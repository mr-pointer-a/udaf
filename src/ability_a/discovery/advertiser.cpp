// advertiser.cpp - Advertiser 实现
#include "advertiser.hpp"

#include "core/log/logger.hpp"
#include "crypto/hmac.hpp"

#include <chrono>
#include <cstring>
#include <openssl/rand.h>
#include <utility>

namespace udaf::ability_a::discovery {

std::unique_ptr<Advertiser>
Advertiser::create(AdvertisementPayload payload,
                   AdvertiserConfig cfg,
                   std::span<const std::uint8_t> psk) noexcept {
    auto a = std::unique_ptr<Advertiser>(new Advertiser());
    a->payload_ = std::move(payload);
    a->cfg_ = cfg;
    if (psk.size() == 32) {
        a->psk_.assign(psk.begin(), psk.end());
    }
    auto s = udaf::ability_a::transport::UdpSocket::create(cfg.bind_port);
    if (s.is_err()) {
        return nullptr;
    }
    a->sock_ = std::move(s).value();
    if (a->sock_->enable_broadcast().is_err()) {
        // 启用广播失败也允许（用于非广播场景）
    }
    return a;
}

Advertiser::~Advertiser() { stop(); }

core::Result<void> Advertiser::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    thread_ = std::thread([this] { run(); });
    return core::Result<void>::ok();
}

void Advertiser::stop() noexcept {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (sock_) sock_->close();
}

core::Result<std::size_t>
Advertiser::broadcast_once() noexcept {
    if (!sock_) {
        return core::Result<std::size_t>::err(core::ErrorCode::NET_SOCKET_CLOSED);
    }
    // header = magic + version + nonce(8B) + mac(32B) + payload
    std::vector<std::uint8_t> header(kDiscoveryHeaderSize);
    header[0] = static_cast<std::uint8_t>((kDiscoveryMagic >> 24) & 0xFF);
    header[1] = static_cast<std::uint8_t>((kDiscoveryMagic >> 16) & 0xFF);
    header[2] = static_cast<std::uint8_t>((kDiscoveryMagic >>  8) & 0xFF);
    header[3] = static_cast<std::uint8_t>(kDiscoveryMagic & 0xFF);
    header[4] = static_cast<std::uint8_t>((kDiscoveryVersion >> 24) & 0xFF);
    header[5] = static_cast<std::uint8_t>((kDiscoveryVersion >> 16) & 0xFF);
    header[6] = static_cast<std::uint8_t>((kDiscoveryVersion >>  8) & 0xFF);
    header[7] = static_cast<std::uint8_t>(kDiscoveryVersion & 0xFF);
    if (RAND_bytes(header.data() + 8, 8) != 1) {
        return core::Result<std::size_t>::err(core::ErrorCode::INTERNAL);
    }
    auto payload_bytes = serialize_payload(payload_);

    // 计算 MAC = HMAC(psk_or_empty, header_8..16 || payload)
    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(8 + payload_bytes.size());
    mac_data.insert(mac_data.end(), header.begin() + 8, header.begin() + 16);
    mac_data.insert(mac_data.end(), payload_bytes.begin(), payload_bytes.end());
    std::vector<std::uint8_t> mac_key;
    if (psk_.size() == 32) mac_key = psk_;
    else mac_key.assign(32, 0);  // 无 PSK 时用全 0（仅用于明文测试）
    auto mac = udaf::crypto::hmac_sha256(mac_key, mac_data);
    if (mac.is_err()) {
        return core::Result<std::size_t>::err(mac.error());
    }
    if (mac.value().size() != 32) {
        return core::Result<std::size_t>::err(core::ErrorCode::INTERNAL);
    }
    std::memcpy(header.data() + 16, mac.value().data(), 32);

    std::vector<std::uint8_t> frame;
    frame.reserve(header.size() + payload_bytes.size());
    frame.insert(frame.end(), header.begin(), header.end());
    frame.insert(frame.end(), payload_bytes.begin(), payload_bytes.end());

    udaf::ability_a::transport::Endpoint ep{
        cfg_.bind_address, cfg_.broadcast_port, true};
    return sock_->send(frame, ep);
}

void Advertiser::run() noexcept {
    while (running_.load()) {
        auto r = broadcast_once();
        if (r.is_err()) {
            udaf::core::Logger::instance().log_with_error(
                udaf::core::LogLevel::Warn, "advertiser broadcast failed",
                r.error());
        }
        // 睡眠（用小切片响应 stop）
        auto end = std::chrono::steady_clock::now() + cfg_.period;
        while (running_.load() &&
               std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

}  // namespace udaf::ability_a::discovery