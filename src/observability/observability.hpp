// observability.hpp - 阶段 E2
//
// 设计要点（ADR-008）：
//   - Meter 10 项内置指标（counter + gauge + histogram）
//   - Tracer：inject/extract KV（K_V_NOOP carrier）
//   - ZmqCarrier：message ↔ KV 映射（inject/extract 归 Tracer）
//   - 性能契约 #20：可观测性开销 < 5% CPU / < 2% 内存

#ifndef UDAF_OBSERVABILITY_OBSERVABILITY_HPP
#define UDAF_OBSERVABILITY_OBSERVABILITY_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace udaf::observability {

// 10 项内置指标（ADR-008 §3.2）
enum class MetricId : std::uint8_t {
    DiscoveryBroadcastTotal   = 0,
    DiscoveryReceiveTotal     = 1,
    HandshakeSuccessTotal     = 2,
    HandshakeFailureTotal     = 3,
    ChannelLatencyMicro       = 4,  // histogram
    NodeLifecycleChangesTotal = 5,
    TopologyUpdatesTotal      = 6,
    AuthFailuresTotal         = 7,
    ActiveConnections         = 8,  // gauge
    PendingEvents             = 9,  // gauge
    kCount
};

[[nodiscard]] inline const char* metric_name(MetricId m) noexcept {
    static constexpr const char* names[] = {
        "discovery_broadcast_total",
        "discovery_receive_total",
        "handshake_success_total",
        "handshake_failure_total",
        "channel_latency_micro",
        "node_lifecycle_changes_total",
        "topology_updates_total",
        "auth_failures_total",
        "active_connections",
        "pending_events",
    };
    return names[static_cast<std::size_t>(m)];
}

// ---------------- Counter / Gauge / Histogram ----------------

class Counter {
public:
    void inc(std::uint64_t v = 1) noexcept { v_.fetch_add(v, std::memory_order_relaxed); }
    void dec(std::uint64_t v = 1) noexcept { v_.fetch_sub(v, std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t value() const noexcept { return v_.load(std::memory_order_relaxed); }
private:
    std::atomic<std::uint64_t> v_{0};
};

class Gauge {
public:
    void set(double v) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        v_ = v;
    }
    void add(double v) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        v_ += v;
    }
    [[nodiscard]] double value() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return v_;
    }
private:
    mutable std::mutex mtx_;
    double v_ = 0.0;
};

// Histogram：固定 8 桶（μs）
class Histogram {
public:
    static constexpr std::size_t kBuckets = 8;
    void observe(std::uint64_t value_us) noexcept;
    [[nodiscard]] std::array<std::uint64_t, kBuckets> snapshot() const noexcept;
    [[nodiscard]] std::uint64_t count() const noexcept;
    [[nodiscard]] std::uint64_t sum() const noexcept;
private:
    mutable std::mutex mtx_;
    std::array<std::uint64_t, kBuckets> buckets_{};
    std::uint64_t count_ = 0;
    std::uint64_t sum_ = 0;
};

// ---------------- Meter ----------------

class Meter {
public:
    /// counter/gauge/histogram 通用记录
    void inc_counter(MetricId m, std::uint64_t v = 1) noexcept;
    void set_gauge(MetricId m, double v) noexcept;
    void observe_histogram(MetricId m, std::uint64_t value_us) noexcept;

    /// 导出 Prometheus 文本格式（性能契约 #20）
    [[nodiscard]] std::string export_prometheus() const noexcept;

private:
    Counter   counters_[static_cast<std::size_t>(MetricId::kCount)];
    Gauge     gauges_[static_cast<std::size_t>(MetricId::kCount)];
    Histogram histograms_[static_cast<std::size_t>(MetricId::kCount)];
};

// ---------------- Tracer ----------------

class Tracer {
public:
    struct Span {
        std::string name;
        std::string trace_id;  // 32-hex
        std::string span_id;   // 16-hex
        std::int64_t start_ns = 0;
        std::int64_t end_ns = 0;
    };

    Span begin(std::string name) noexcept;
    void end(Span& s) noexcept;

    /// KV 注入/提取（headers-style）
    void inject(const Span& s, std::unordered_map<std::string, std::string>& kv) const noexcept;
    [[nodiscard]] Span extract(std::string_view name,
                                const std::unordered_map<std::string, std::string>& kv) noexcept;
};

}  // namespace udaf::observability

#endif  // UDAF_OBSERVABILITY_OBSERVABILITY_HPP