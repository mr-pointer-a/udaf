// test_observability.cpp - 阶段 E2
#include <gtest/gtest.h>

#include "observability/observability.hpp"

#include <chrono>
#include <thread>

namespace obs = udaf::observability;

TEST(UdafObs_Counter, IncAndValue) {
    obs::Counter c;
    c.inc(3);
    c.inc();
    EXPECT_EQ(c.value(), 4u);
    c.dec(2);
    EXPECT_EQ(c.value(), 2u);
}

TEST(UdafObs_Gauge, SetAndAdd) {
    obs::Gauge g;
    g.set(1.5);
    EXPECT_DOUBLE_EQ(g.value(), 1.5);
    g.add(0.25);
    EXPECT_DOUBLE_EQ(g.value(), 1.75);
}

TEST(UdafObs_Histogram, ObserveAndBuckets) {
    obs::Histogram h;
    h.observe(50);
    h.observe(500);
    h.observe(5000);
    h.observe(999999);
    EXPECT_EQ(h.count(), 4u);
    auto snap = h.snapshot();
    EXPECT_EQ(snap[0], 1u);
    EXPECT_GE(snap[7], 1u);
    EXPECT_GT(h.sum(), 0u);
}

TEST(UdafObs_Meter, ExportPrometheus) {
    obs::Meter m;
    m.inc_counter(obs::MetricId::DiscoveryBroadcastTotal, 7);
    m.set_gauge(obs::MetricId::ActiveConnections, 3.0);
    m.observe_histogram(obs::MetricId::ChannelLatencyMicro, 1234);
    auto txt = m.export_prometheus();
    EXPECT_NE(txt.find("discovery_broadcast_total"), std::string::npos);
    EXPECT_NE(txt.find("active_connections"), std::string::npos);
    EXPECT_NE(txt.find("channel_latency_micro_histogram_count"), std::string::npos);
}

TEST(UdafObs_Meter, AllTenBuiltinNames) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(obs::MetricId::kCount); ++i) {
        EXPECT_NE(obs::metric_name(static_cast<obs::MetricId>(i)), nullptr);
    }
    EXPECT_STREQ(obs::metric_name(obs::MetricId::DiscoveryBroadcastTotal), "discovery_broadcast_total");
    EXPECT_STREQ(obs::metric_name(obs::MetricId::PendingEvents), "pending_events");
}

TEST(UdafObs_Tracer, BeginEndAndInjectExtract) {
    obs::Tracer t;
    auto s = t.begin("test_op");
    EXPECT_FALSE(s.trace_id.empty());
    EXPECT_EQ(s.trace_id.size(), 32u);
    EXPECT_EQ(s.span_id.size(), 16u);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    t.end(s);
    EXPECT_GT(s.end_ns, s.start_ns);

    std::unordered_map<std::string, std::string> kv;
    t.inject(s, kv);
    EXPECT_NE(kv.find("traceparent"), kv.end());

    auto s2 = t.extract("remote_op", kv);
    EXPECT_EQ(s2.trace_id, s.trace_id);
    EXPECT_EQ(s2.span_id, s.span_id);
}

TEST(UdafObs_Meter, Overhead) {
    obs::Meter m;
    constexpr int N = 100000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        m.inc_counter(obs::MetricId::DiscoveryBroadcastTotal);
        m.set_gauge(obs::MetricId::ActiveConnections, static_cast<double>(i));
    }
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    EXPECT_LT(us, 5'000'000) << "overhead_us=" << us;
}