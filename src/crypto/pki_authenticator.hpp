// pki_authenticator.hpp - PKI 模式 Authenticator 实现
#ifndef UDAF_CRYPTO_PKI_AUTHENTICATOR_HPP
#define UDAF_CRYPTO_PKI_AUTHENTICATOR_HPP

#include <filesystem>
#include <memory>
#include <span>

#include "auth_types.hpp"
#include "authenticator.hpp"
#include "core/result.hpp"
#include "tls_context.hpp"

namespace udaf::crypto {

/// PKI 模式 Authenticator：基于 TLS 1.3 完整握手 + 自带对称加密（PSK 部分通过 TLS 派生）。
class PkiAuthenticator : public Authenticator {
public:
    /// 服务端工厂
    static std::unique_ptr<PkiAuthenticator>
    create_server(const std::filesystem::path& cert_file,
                  const std::filesystem::path& key_file) noexcept;

    /// 客户端工厂（可自带 mTLS 证书）
    static std::unique_ptr<PkiAuthenticator>
    create_client(const std::filesystem::path& ca_file,
                  const std::filesystem::path& cert_file = {},
                  const std::filesystem::path& key_file = {}) noexcept;

    ~PkiAuthenticator() override = default;

    // PSK Authenticator 的语义在 PKI 下简化为：
    //   begin_handshake/process_handshake 返回空字节流（握手由 TLS 内部完成，
    //   PkiAuthenticator 仅维护一个虚拟状态）；调用方需直接走 PkiHandshake。
    core::Result<std::vector<std::uint8_t>> begin_handshake() noexcept override;
    core::Result<std::vector<std::uint8_t>> process_handshake(
        std::span<const std::uint8_t> peer_msg) noexcept override;
    [[nodiscard]] bool is_handshake_done() const noexcept override { return true; }

    /// PKI 下 TLS 内部已处理加密，encrypt/decrypt 视为 no-op 包装。
    core::Result<std::vector<std::uint8_t>> encrypt(
        std::span<const std::uint8_t> plaintext) noexcept override;
    core::Result<std::vector<std::uint8_t>> decrypt(
        std::span<const std::uint8_t> ciphertext) noexcept override;

    core::Result<DerivedKeys> session_keys() noexcept override;

    /// 访问内部 TlsContext（用于 PkiHandshake）
    [[nodiscard]] TlsContext* context() const noexcept { return ctx_.get(); }

private:
    PkiAuthenticator() = default;
    std::unique_ptr<TlsContext> ctx_;
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_PKI_AUTHENTICATOR_HPP