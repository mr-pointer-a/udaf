// scanner.cpp - Scanner 实现
#include "scanner.hpp"

#include "core/log/logger.hpp"
#include "crypto/hmac.hpp"

#include <cstring>
#include <thread>
#include <utility>

namespace udaf::ability_a::discovery {

std::unique_ptr<Scanner>
Scanner::create(udaf::ability_a::registry::ServiceRegistry* registry,
                ScannerConfig cfg,
                std::span<const std::uint8_t> psk) noexcept {
    auto s = std::unique_ptr<Scanner>(new Scanner());
    s->registry_ = registry;
    s->cfg_ = cfg;
    if (psk.size() == 32) {
        s->psk_.assign(psk.begin(), psk.end());
    }
    auto sock = udaf::ability_a::transport::UdpSocket::create(cfg.bind_port);
    if (sock.is_err()) return nullptr;
    s->sock_ = std::move(sock).value();
    return s;
}

Scanner::~Scanner() { stop(); }

core::Result<void> Scanner::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    thread_ = std::thread([this] {
        while (running_.load()) {
            auto r = poll_once();
            if (r.is_err() && r.error() != udaf::core::ErrorCode::NET_TIMEOUT) {
                udaf::core::Logger::instance().log_with_error(
                    udaf::core::LogLevel::Warn, "scanner poll error", r.error());
            }
        }
    });
    return core::Result<void>::ok();
}

void Scanner::stop() noexcept {
    if (!running_.exchange(false)) return;
    if (sock_) sock_->close();
    if (thread_.joinable()) thread_.join();
}

core::Result<bool> Scanner::poll_once() noexcept {
    if (!sock_) {
        return core::Result<bool>::err(udaf::core::ErrorCode::NET_SOCKET_CLOSED);
    }
    auto r = sock_->recv(100);
    if (r.is_err()) {
        return core::Result<bool>::err(r.error());
    }
    return handle_frame(r.value());
}

core::Result<bool>
Scanner::handle_frame(std::span<const std::uint8_t> frame) noexcept {
    if (frame.size() < kDiscoveryHeaderSize) {
        return core::Result<bool>::err(udaf::core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }
    // 校验 magic
    std::uint32_t magic = (static_cast<std::uint32_t>(frame[0]) << 24) |
                          (static_cast<std::uint32_t>(frame[1]) << 16) |
                          (static_cast<std::uint32_t>(frame[2]) <<  8) |
                           static_cast<std::uint32_t>(frame[3]);
    if (magic != kDiscoveryMagic) {
        return core::Result<bool>::err(udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }
    std::uint32_t version = (static_cast<std::uint32_t>(frame[4]) << 24) |
                            (static_cast<std::uint32_t>(frame[5]) << 16) |
                            (static_cast<std::uint32_t>(frame[6]) <<  8) |
                             static_cast<std::uint32_t>(frame[7]);
    if (version != kDiscoveryVersion) {
        return core::Result<bool>::err(udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }

    // 提取 nonce（8B @ 8..16）
    char nonce_hex[17] = {};
    static const char* hex = "0123456789abcdef";
    for (std::size_t i = 0; i < 8; ++i) {
        const auto b = frame[8 + i];
        nonce_hex[i*2]   = hex[(b >> 4) & 0xF];
        nonce_hex[i*2+1] = hex[b & 0xF];
    }
    std::string nonce_str(nonce_hex, 16);

    // 重放防护
    {
        auto now = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> lk(nonces_mtx_);
        // 清理过期
        for (auto it = seen_nonces_.begin(); it != seen_nonces_.end(); ) {
            if (now - it->second > cfg_.replay_window) {
                it = seen_nonces_.erase(it);
            } else {
                ++it;
            }
        }
        if (seen_nonces_.count(nonce_str)) {
            return core::Result<bool>::ok(false);  // 已见过，静默忽略
        }
        seen_nonces_[nonce_str] = now;
    }

    // MAC 校验
    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(8 + (frame.size() - kDiscoveryHeaderSize));
    mac_data.insert(mac_data.end(), frame.begin() + 8, frame.begin() + 16);
    mac_data.insert(mac_data.end(),
                     frame.begin() + kDiscoveryHeaderSize, frame.end());
    std::vector<std::uint8_t> mac_key =
        (psk_.size() == 32) ? psk_ : std::vector<std::uint8_t>(32, 0);
    std::array<std::uint8_t, 32> expected{};
    std::memcpy(expected.data(), frame.data() + 16, 32);
    auto v = udaf::crypto::hmac_sha256_verify(mac_key, mac_data, expected);
    if (v.is_err()) {
        return core::Result<bool>::ok(false);  // MAC 失败静默丢弃
    }

    // 解析 payload + 写入 registry
    auto payload = parse_payload(frame.subspan(kDiscoveryHeaderSize));
    if (payload.node_id.empty()) {
        return core::Result<bool>::err(udaf::core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }
    if (registry_) {
        udaf::ability_a::registry::RegistryEntry entry;
        entry.node_id_      = std::move(payload.node_id);
        entry.hostname_     = std::move(payload.hostname);
        entry.bind_address_ = std::move(payload.bind_address);
        entry.bind_port_    = payload.bind_port;
        for (auto& s : payload.services) {
            udaf::ability_a::registry::ServiceDescriptor sd;
            sd.service_name = std::move(s);
            entry.services_.push_back(std::move(sd));
        }
        (void)registry_->register_node(entry);
    }
    return core::Result<bool>::ok(true);
}

std::size_t Scanner::seen_count() const noexcept {
    std::shared_lock<std::shared_mutex> lk(nonces_mtx_);
    return seen_nonces_.size();
}

void Scanner::clear_nonces() noexcept {
    std::unique_lock<std::shared_mutex> lk(nonces_mtx_);
    seen_nonces_.clear();
}

}  // namespace udaf::ability_a::discovery