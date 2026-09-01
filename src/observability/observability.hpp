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

/// @brief 内置指标 ID 枚举，共 10 项（ADR-008 §3.2）。
///
/// 每项指标可独立用作 counter / gauge / histogram 三种类型之一，
/// 具体语义由 m 的取值决定（见注释）。kCount 为哨兵值，不可作为指标 ID。
enum class MetricId : std::uint8_t {
    DiscoveryBroadcastTotal   = 0,  ///< 发现协议广播总数（counter）
    DiscoveryReceiveTotal     = 1,  ///< 发现协议接收总数（counter）
    HandshakeSuccessTotal     = 2,  ///< 握手成功总数（counter）
    HandshakeFailureTotal     = 3,  ///< 握手失败总数（counter）
    ChannelLatencyMicro       = 4,  ///< 通道延迟（histogram，单位 μs）
    NodeLifecycleChangesTotal = 5,  ///< 节点生命周期变化总数（counter）
    TopologyUpdatesTotal      = 6,  ///< 拓扑更新总数（counter）
    AuthFailuresTotal         = 7,  ///< 认证失败总数（counter）
    ActiveConnections         = 8,  ///< 当前活跃连接数（gauge）
    PendingEvents             = 9,  ///< 当前待处理事件数（gauge）
    kCount                          ///< 哨兵：指标总数（不可作为 ID 使用）
};

/// @brief 将 MetricId 转换为稳定的 snake_case Prometheus 指标名。
/// @param m 任意 MetricId 值（< kCount）
/// @return 字符串字面量（static lifetime，永不返回 nullptr）
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

/// @brief 原子 64 位计数器（单调递增）。
///
/// 用于事件类指标的累计计数（成功握手次数、接收包数等）。
/// 内部使用 std::atomic<std::uint64_t>，inc/dec 为 lock-free。
class Counter {
public:
    /// @brief 原子地增加计数。
    /// @param v 增量，默认 1
    void inc(std::uint64_t v = 1) noexcept { v_.fetch_add(v, std::memory_order_relaxed); }

    /// @brief 原子地减少计数。
    /// @param v 减量，默认 1
    void dec(std::uint64_t v = 1) noexcept { v_.fetch_sub(v, std::memory_order_relaxed); }

    /// @brief 当前累计值。
    [[nodiscard]] std::uint64_t value() const noexcept { return v_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t> v_{0};
};

/// @brief 互斥保护的双精度仪表盘（可增可减）。
///
/// 用于可上下波动的瞬时值（活跃连接数、待处理事件数等）。
/// 由于 double 读写不是原子的，内部用 mutex 串行化。
class Gauge {
public:
    /// @brief 覆盖式设置当前值。
    void set(double v) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        v_ = v;
    }

    /// @brief 累加式更新当前值。
    void add(double v) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        v_ += v;
    }

    /// @brief 当前仪表盘读数。
    [[nodiscard]] double value() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return v_;
    }

private:
    mutable std::mutex mtx_;
    double v_ = 0.0;
};

/// @brief 固定 8 桶直方图，桶边界以微秒为单位对数分布。
///
/// 用于延迟分布统计（通道时延、握手耗时等）。
/// 桶边界：1 / 10 / 100 / 1k / 10k / 100k / 1M / 10M μs
class Histogram {
public:
    static constexpr std::size_t kBuckets = 8;  ///< 桶数量（固定 8）

    /// @brief 记录一次观测值。
    /// @param value_us 微秒为单位的非负整数
    void observe(std::uint64_t value_us) noexcept;

    /// @brief 复制一份桶内累计计数。
    [[nodiscard]] std::array<std::uint64_t, kBuckets> snapshot() const noexcept;

    /// @brief 已观测的总次数。
    [[nodiscard]] std::uint64_t count() const noexcept;

    /// @brief 已观测值的累计和（用于计算 P 均值）。
    [[nodiscard]] std::uint64_t sum() const noexcept;

private:
    mutable std::mutex mtx_;
    std::array<std::uint64_t, kBuckets> buckets_{};
    std::uint64_t count_ = 0;
    std::uint64_t sum_ = 0;
};

// ---------------- Meter ----------------

/// @brief 聚合所有内置指标的注册表。
///
/// 持有 10 项指标的 counter / gauge / histogram 三个数组视图。
/// 任何模块可通过 inc_counter / set_gauge / observe_histogram
/// 自由记录，再通过 export_prometheus 周期导出。
class Meter {
public:
    /// @brief 累加指定 counter 指标。
    /// @param m 指标 ID
    /// @param v 增量，默认 1
    void inc_counter(MetricId m, std::uint64_t v = 1) noexcept;

    /// @brief 覆盖式设置指定 gauge 指标。
    /// @param m 指标 ID
    /// @param v 新值
    void set_gauge(MetricId m, double v) noexcept;

    /// @brief 记录一次 histogram 观测。
    /// @param m 指标 ID
    /// @param value_us 微秒单位的观测值
    void observe_histogram(MetricId m, std::uint64_t value_us) noexcept;

    /// @brief 导出 Prometheus 文本格式（性能契约 #20）。
    /// @return 多行文本，可直接 HTTP 响应到 /metrics 端点
    [[nodiscard]] std::string export_prometheus() const noexcept;

private:
    Counter   counters_[static_cast<std::size_t>(MetricId::kCount)];
    Gauge     gauges_[static_cast<std::size_t>(MetricId::kCount)];
    Histogram histograms_[static_cast<std::size_t>(MetricId::kCount)];
};

// ---------------- Tracer ----------------

/// @brief 简易追踪器，提供 Span 创建与 KV 注入/提取。
///
/// 仅实现 OpenTelemetry 风格的 traceparent header 语义子集：
/// trace_id 32-hex、span_id 16-hex。完整 OTLP 导出不在范围内。
class Tracer {
public:
    /// @brief 单个 Span 的内存表示。
    struct Span {
        std::string name;        ///< Span 名（操作描述，如 "psk_handshake"）
        std::string trace_id;    ///< 32-hex 字符 trace ID
        std::string span_id;     ///< 16-hex 字符 span ID
        std::int64_t start_ns = 0;  ///< CLOCK_MONOTONIC 起始纳秒
        std::int64_t end_ns = 0;    ///< CLOCK_MONOTONIC 结束纳秒（end 后填充）
    };

    /// @brief 创建并启动一个 Span。
    /// @param name 操作名（如 "channel_recv"）
    /// @return 已填充 trace_id / span_id / start_ns 的 Span
    static Span begin(std::string name) noexcept;

    /// @brief 结束 Span 并填充 end_ns。
    /// @param s begin 返回的 Span 引用
    static void end(Span& s) noexcept;

    /// @brief 将 Span 信息注入到 KV map（模拟 HTTP/Message header）。
    /// @param s 来源 Span
    /// @param kv 目标 KV map，会新增/覆盖 traceparent / tracestate
    static void inject(const Span& s, std::unordered_map<std::string, std::string>& kv) noexcept;

    /// @brief 从 KV map 中恢复 Span 信息。
    /// @param name 新 Span 的操作名
    /// @param kv 来源 KV map
    /// @return 已填充 trace_id / span_id 的 Span；缺字段时返回空 trace_id
    [[nodiscard]] static Span extract(std::string_view name,
                                const std::unordered_map<std::string, std::string>& kv) noexcept;
};

}  // namespace udaf::observability

#endif  // UDAF_OBSERVABILITY_OBSERVABILITY_HPP