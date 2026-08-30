// observability.cpp
#include "observability.hpp"

#include <openssl/rand.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>

namespace udaf::observability {

namespace {

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void to_hex(unsigned char* dst, std::size_t n, std::string& out) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.clear();
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kHex[(dst[i] >> 4) & 0xf]);
        out.push_back(kHex[dst[i] & 0xf]);
    }
}

}  // namespace

void Histogram::observe(std::uint64_t value_us) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    static constexpr std::uint64_t kEdges[Histogram::kBuckets] = {
        100, 500, 1000, 5000, 10000, 50000, 100000, 1000000
    };
    for (std::size_t i = 0; i < kBuckets; ++i) {
        if (value_us <= kEdges[i]) buckets_[i]++;
    }
    count_++;
    sum_ += value_us;
}

std::array<std::uint64_t, Histogram::kBuckets> Histogram::snapshot() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return buckets_;
}

std::uint64_t Histogram::count() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return count_;
}

std::uint64_t Histogram::sum() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return sum_;
}

void Meter::inc_counter(MetricId m, std::uint64_t v) noexcept {
    counters_[static_cast<std::size_t>(m)].inc(v);
}

void Meter::set_gauge(MetricId m, double v) noexcept {
    gauges_[static_cast<std::size_t>(m)].set(v);
}

void Meter::observe_histogram(MetricId m, std::uint64_t value_us) noexcept {
    histograms_[static_cast<std::size_t>(m)].observe(value_us);
}

std::string Meter::export_prometheus() const noexcept {
    std::ostringstream oss;
    for (std::size_t i = 0; i < static_cast<std::size_t>(MetricId::kCount); ++i) {
        const char* name = metric_name(static_cast<MetricId>(i));
        // counter / gauge
        oss << "# TYPE " << name << " counter\n";
        oss << name << " " << counters_[i].value() << "\n";
        oss << "# TYPE " << name << "_gauge gauge\n";
        oss << name << "_gauge " << gauges_[i].value() << "\n";
        // histogram
        auto h = histograms_[i].snapshot();
        oss << "# TYPE " << name << "_histogram histogram\n";
        oss << name << "_histogram_count " << histograms_[i].count() << "\n";
        oss << name << "_histogram_sum " << histograms_[i].sum() << "\n";
        for (std::size_t b = 0; b < h.size(); ++b) {
            oss << name << "_histogram_bucket{" << b << "} " << h[b] << "\n";
        }
    }
    return oss.str();
}

Tracer::Span Tracer::begin(std::string name) noexcept {
    Span s;
    s.name = std::move(name);
    s.start_ns = now_ns();
    unsigned char buf[24];
    ::RAND_bytes(buf, sizeof(buf));
    std::string hex;
    to_hex(buf, 8, s.trace_id);
    to_hex(buf + 8, 8, hex); // 16 hex chars (高 16)
    s.trace_id += hex.substr(0, 16);
    to_hex(buf + 16, 8, s.span_id);
    return s;
}

void Tracer::end(Span& s) noexcept { s.end_ns = now_ns(); }

void Tracer::inject(const Span& s, std::unordered_map<std::string, std::string>& kv) const noexcept {
    kv["traceparent"] = "00-" + s.trace_id + "-" + s.span_id + "-01";
}

Tracer::Span Tracer::extract(std::string_view name,
                              const std::unordered_map<std::string, std::string>& kv) noexcept {
    Span s;
    s.name = std::string(name);
    auto it = kv.find("traceparent");
    if (it != kv.end()) {
        // 00-<32hex>-<16hex>-<2hex>
        const std::string& v = it->second;
        if (v.size() >= 55) {
            s.trace_id = v.substr(3, 32);
            s.span_id  = v.substr(36, 16);
        }
    }
    s.start_ns = now_ns();
    return s;
}

}  // namespace udaf::observability