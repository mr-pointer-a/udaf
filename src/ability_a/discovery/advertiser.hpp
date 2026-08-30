// advertiser.hpp - 主动广播者
//
// 设计要点：
//   - 定期广播 AdvertisementPayload（CLAUDE.md §3.8 防 O(N²) 风暴：广播 1/30s）
//   - 后台 std::thread + std::atomic<bool> running
//   - 加密走 PSK AEAD（auth_types::SecretKey）

#ifndef UDAF_ABILITY_A_DISCOVERY_ADVERTISER_HPP
#define UDAF_ABILITY_A_DISCOVERY_ADVERTISER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "advertisement.hpp"
#include "ability_a/transport/udp_socket.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::discovery {

/// Advertiser 配置
struct AdvertiserConfig {
    std::string bind_address  = "127.0.0.1";
    std::uint16_t bind_port   = 0;             // 0 = 自动
    std::uint16_t broadcast_port = 19999;
    std::chrono::seconds period{30};
};

class Advertiser {
public:
    /// @param payload 待广播内容（明文）
    /// @param psk 可选 PSK（32B）用于加密 payload
    static std::unique_ptr<Advertiser>
    create(AdvertisementPayload payload,
           AdvertiserConfig cfg,
           std::span<const std::uint8_t> psk = {}) noexcept;

    ~Advertiser();
    Advertiser(const Advertiser&) = delete;
    Advertiser& operator=(const Advertiser&) = delete;
    Advertiser(Advertiser&&) = delete;
    Advertiser& operator=(Advertiser&&) = delete;

    /// 启动后台广播线程
    core::Result<void> start() noexcept;
    /// 停止
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    /// 手动触发一次广播（测试用）
    [[nodiscard]] core::Result<std::size_t>
    broadcast_once() noexcept;

private:
    Advertiser() = default;

    void run() noexcept;

    AdvertisementPayload payload_;
    AdvertiserConfig cfg_;
    std::vector<std::uint8_t> psk_;
    std::unique_ptr<udaf::ability_a::transport::UdpSocket> sock_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace udaf::ability_a::discovery

#endif  // UDAF_ABILITY_A_DISCOVERY_ADVERTISER_HPP