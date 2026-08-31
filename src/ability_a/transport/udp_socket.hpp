// udp_socket.hpp - UDP socket RAII + 频率限制
//
// 设计要点：
//   - 单播 5/s（每目标地址独立限流）
//   - 广播 1/30s（所有广播共享一个令牌桶）
//   - Rule of Five：禁止拷贝
//   - PSK 加密走 udaf::crypto
//   - 不抛异常（CLAUDE.md §3.5）

#ifndef UDAF_ABILITY_A_TRANSPORT_UDP_SOCKET_HPP
#define UDAF_ABILITY_A_TRANSPORT_UDP_SOCKET_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::transport {

/// 目标端点（IPv4 + port）
struct Endpoint {
    std::string address;
    std::uint16_t port = 0;
    bool is_broadcast = false;
};

/// UdpSocket：RAII 包装 + 单播/广播频率限制
class UdpSocket {
public:
    /// 构造函数：绑定端口（port=0 表示自动分配）
    static core::Result<std::unique_ptr<UdpSocket>> create(std::uint16_t bind_port = 0) noexcept;

    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) = delete;
    UdpSocket& operator=(UdpSocket&&) = delete;

    /// 发送明文（限流在内部）
    /// 超过单播 5/s 或广播 1/30s 时返回 NET_RATE_LIMIT
    [[nodiscard]] core::Result<std::size_t>
    send(std::span<const std::uint8_t> payload, const Endpoint& dst) noexcept;

    /// 接收（timeout_ms=-1 表示阻塞，0 表示非阻塞）
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    recv(int timeout_ms) noexcept;

    /// 启用广播（需要 bind 时已设置 SO_BROADCAST）
    [[nodiscard]] core::Result<void> enable_broadcast() const noexcept;

    /// 启用 PSK 加密（nullptr 表示关闭）
    void set_psk(std::span<const std::uint8_t> psk) noexcept;

    /// 关闭 socket
    void close() noexcept;

    /// 获取绑定端口
    [[nodiscard]] std::uint16_t bound_port() const noexcept;

private:
    UdpSocket() = default;

    /// 检查并更新单播速率限制
    bool check_unicast_rate(const std::string& addr) noexcept;
    /// 检查并更新广播速率限制
    bool check_broadcast_rate() noexcept;

    mutable int fd_ = -1;
    std::uint16_t bound_port_ = 0;
    std::vector<std::uint8_t> psk_;

    // 速率限制
    static constexpr int kUnicastMaxPerSec  = 5;
    static constexpr std::chrono::milliseconds kBroadcastPeriod{30000};

    std::mutex unicast_mtx_;
    std::unordered_map<std::string,
        std::vector<std::chrono::steady_clock::time_point>> unicast_history_;
    std::chrono::steady_clock::time_point last_broadcast_at_;
};

}  // namespace udaf::ability_a::transport

#endif  // UDAF_ABILITY_A_TRANSPORT_UDP_SOCKET_HPP