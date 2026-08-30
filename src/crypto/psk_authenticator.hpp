// psk_authenticator.hpp - PSK 模式 Authenticator 实现
#ifndef UDAF_CRYPTO_PSK_AUTHENTICATOR_HPP
#define UDAF_CRYPTO_PSK_AUTHENTICATOR_HPP

#include <memory>
#include <span>

#include "auth_types.hpp"
#include "authenticator.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// PSK 模式 Authenticator：使用 HKDF-SHA256 派生 + AES-256-GCM AEAD。
/// 性能契约：握手 P95 < 2ms（02 §3.4 / 03 §11.2）。
class PskAuthenticator : public Authenticator {
public:
    /// 创建客户端 Authenticator
    static std::unique_ptr<PskAuthenticator>
    create_client(std::span<const std::uint8_t> psk,
                  std::string_view identity) noexcept;

    /// 创建服务端 Authenticator
    static std::unique_ptr<PskAuthenticator>
    create_server(std::span<const std::uint8_t> psk,
                  std::string_view identity) noexcept;

    ~PskAuthenticator() override = default;

    core::Result<std::vector<std::uint8_t>> begin_handshake() noexcept override;
    core::Result<std::vector<std::uint8_t>> process_handshake(
        std::span<const std::uint8_t> peer_msg) noexcept override;
    [[nodiscard]] bool is_handshake_done() const noexcept override { return done_; }
    core::Result<std::vector<std::uint8_t>> encrypt(
        std::span<const std::uint8_t> plaintext) noexcept override;
    core::Result<std::vector<std::uint8_t>> decrypt(
        std::span<const std::uint8_t> ciphertext) noexcept override;
    core::Result<DerivedKeys> session_keys() noexcept override;

private:
    PskAuthenticator() = default;

    SecretKey   psk_;
    std::string identity_;
    AuthRequest client_request_{};
    AuthResponse server_response_{};
    DerivedKeys keys_{};
    bool        is_client_ = false;
    bool        done_ = false;
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_PSK_AUTHENTICATOR_HPP