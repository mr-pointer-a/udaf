// pki_authenticator.cpp - PkiAuthenticator 实现
#include "pki_authenticator.hpp"

namespace udaf::crypto {

std::unique_ptr<PkiAuthenticator>
PkiAuthenticator::create_server(const std::filesystem::path& cert_file,
                                const std::filesystem::path& key_file) noexcept {
    auto ctx = TlsContext::create_server_pki(cert_file, key_file);
    if (!ctx) return nullptr;
    auto a = std::unique_ptr<PkiAuthenticator>(new PkiAuthenticator());
    a->ctx_ = std::move(ctx);
    return a;
}

std::unique_ptr<PkiAuthenticator>
PkiAuthenticator::create_client(const std::filesystem::path& ca_file,
                                const std::filesystem::path& cert_file,
                                const std::filesystem::path& key_file) noexcept {
    auto ctx = TlsContext::create_client_pki(ca_file, cert_file, key_file);
    if (!ctx) return nullptr;
    auto a = std::unique_ptr<PkiAuthenticator>(new PkiAuthenticator());
    a->ctx_ = std::move(ctx);
    return a;
}

core::Result<std::vector<std::uint8_t>> PkiAuthenticator::begin_handshake() noexcept {
    // PKI 握手由 TLS 内部驱动，这里返回空序列
    return core::Result<std::vector<std::uint8_t>>::ok({});
}

core::Result<std::vector<std::uint8_t>>
PkiAuthenticator::process_handshake(std::span<const std::uint8_t> /*peer_msg*/) noexcept {
    // PKI 模式下 process_handshake 由 PkiHandshake::step 直接驱动
    return core::Result<std::vector<std::uint8_t>>::ok({});
}

core::Result<std::vector<std::uint8_t>>
PkiAuthenticator::encrypt(std::span<const std::uint8_t> plaintext) noexcept {
    // TLS 加密由 PkiHandshake 负责；此处返回原文（由调用方改走 PkiHandshake::encrypt）
    return core::Result<std::vector<std::uint8_t>>::ok(
        std::vector<std::uint8_t>(plaintext.begin(), plaintext.end()));
}

core::Result<std::vector<std::uint8_t>>
PkiAuthenticator::decrypt(std::span<const std::uint8_t> ciphertext) noexcept {
    return core::Result<std::vector<std::uint8_t>>::ok(
        std::vector<std::uint8_t>(ciphertext.begin(), ciphertext.end()));
}

core::Result<DerivedKeys> PkiAuthenticator::session_keys() noexcept {
    // PKI 模式下会话密钥由 TLS 内部管理
    return core::Result<DerivedKeys>::err(core::ErrorCode::INTERNAL);
}

}  // namespace udaf::crypto