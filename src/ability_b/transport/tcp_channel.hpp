// tcp_channel.hpp - 跨主机 ZMQ tcp:// 通道
//
// 设计要点：
//   - PIMPL 持有 zmq::context_t + zmq::socket_t (DEALER 模式)
//   - 指数退避重连（初值 100ms，上限 30s）
//   - 连接超时 / 不可达 → 返回 Error / Timeout
//   - 三优先级子队列：HEARTBEAT 始终强制投递
//   - 不引入异常（CLAUDE.md §3.5）
//   - 当前实现：plain tcp://；TLS 1.3 为后续阶段（评审 C-9 跟踪）
//
// 设计依据：docs/03-detailed-design.md §3.3.5 + docs/04-module-design.md §2.5

#ifndef UDAF_ABILITY_B_TRANSPORT_TCP_CHANNEL_HPP
#define UDAF_ABILITY_B_TRANSPORT_TCP_CHANNEL_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "channel.hpp"

namespace udaf::ability_b::transport {

/// TcpChannel 配置
struct TcpChannelConfig {
    /// ZMQ 端点（如 "tcp://127.0.0.1:5555"）
    std::string connect_uri;
    /// 单次连接超时（默认 5000ms）
    std::chrono::milliseconds connect_timeout{5000};
    /// 重连初始退避（默认 100ms）
    std::chrono::milliseconds reconnect_backoff_init{100};
    /// 重连最大退避（默认 30000ms）
    std::chrono::milliseconds reconnect_backoff_max{30000};
    /// ZMQ_SNDTIMEO / ZMQ_RCVTIMEO 默认值（默认 5000ms）
    std::chrono::milliseconds io_timeout{5000};
    /// 高水位标记（默认 1000 帧）
    int send_hwm{1000};
    /// 接收高水位标记
    int recv_hwm{1000};
};

/// 跨主机 ZMQ tcp:// 通道（PIMPL 持有 context + socket）
class TcpChannel final : public ChannelBase {
public:
    /// @param cfg 连接配置
    /// @param auto_connect 是否立即尝试连接（默认 true）
    explicit TcpChannel(TcpChannelConfig cfg, bool auto_connect = true);
    ~TcpChannel() override;

    TcpChannel(const TcpChannel&) = delete;
    TcpChannel& operator=(const TcpChannel&) = delete;
    TcpChannel(TcpChannel&&) = delete;
    TcpChannel& operator=(TcpChannel&&) = delete;

    [[nodiscard]] SendResult send_base(MessageFrame m) noexcept override;
    [[nodiscard]] RecvStatus recv_base(MessageFrame& m, int timeout_ms) noexcept override;
    void close() noexcept override;
    [[nodiscard]] TransportType transport() const noexcept override {
        return TransportType::Tcp;
    }

    /// 主动尝试连接（连接已在 auto_connect=true 时创建）
    /// @return true 连接成功；false 不可达 / 超时
    bool connect() noexcept;

    /// 是否已建立连接
    [[nodiscard]] bool is_connected() const noexcept { return connected_.load(std::memory_order_acquire); }

    /// 最近一次错误码（错误时通过 get_last_error 查询）
    [[nodiscard]] core::ErrorCode last_error() const noexcept {
        return last_error_.load(std::memory_order_acquire);
    }

    /// ZMQ 端点
    [[nodiscard]] const std::string& endpoint() const noexcept { return cfg_.connect_uri; }

private:
    /// PIMPL：隐藏 zmq::context_t / zmq::socket_t 等 C++ 类型
    struct Impl;

    /// 帧编码：优先级(1B) + seq(8B LE) + payload 长度(4B LE) + payload
    /// 解码对称
    void close_socket_locked() noexcept;

    TcpChannelConfig cfg_;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> closed_{false};
    std::atomic<std::uint64_t> next_seq_{0};
    std::atomic<core::ErrorCode> last_error_{core::ErrorCode::OK};
    std::mutex mtx_;  // 保护 impl_ 的连接/重连状态
};

}  // namespace udaf::ability_b::transport

#endif  // UDAF_ABILITY_B_TRANSPORT_TCP_CHANNEL_HPP