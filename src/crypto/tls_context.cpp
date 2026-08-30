// tls_context.cpp - TLS 1.3 上下文 PIMPL 实现
#include "tls_context.hpp"

#include "core/log/logger.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/tls1.h>

#include <algorithm>
#include <cstring>

namespace udaf::crypto {

// Impl 在 PIMPL 实现文件内定义；公开（struct）以便回调函数访问
struct TlsContext::Impl {
    SSL_CTX* ctx = nullptr;
    Mode     mode;
    std::vector<std::uint8_t> psk;
    std::string identity;

    explicit Impl(Mode m) : mode(m) {}
    ~Impl() {
        if (ctx) SSL_CTX_free(ctx);
    }
};

namespace {

void enable_tls13_groups(SSL_CTX* ctx) noexcept {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    static const char* groups = "X25519:P-256:P-384";
    SSL_CTX_set1_groups_list(ctx, groups);
#endif
}

// 服务端 PSK 回调：4 参数签名
unsigned int psk_server_cb(SSL* ssl,
                           const char* /*identity*/,
                           unsigned char* psk_out,
                           unsigned int max_psk_len) noexcept {
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    if (!ctx) return 0;
    TlsContext::Impl* impl =
        static_cast<TlsContext::Impl*>(SSL_CTX_get_app_data(ctx));
    if (!impl || impl->psk.size() > max_psk_len) return 0;
    std::memcpy(psk_out, impl->psk.data(), impl->psk.size());
    return static_cast<unsigned int>(impl->psk.size());
}

// 客户端 PSK 回调：6 参数签名（需要写 identity）
unsigned int psk_client_cb(SSL* ssl,
                           const char* /*hint*/,
                           char* identity_out,
                           unsigned int max_identity_len,
                           unsigned char* psk_out,
                           unsigned int max_psk_len) noexcept {
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    if (!ctx) return 0;
    TlsContext::Impl* impl =
        static_cast<TlsContext::Impl*>(SSL_CTX_get_app_data(ctx));
    if (!impl) return 0;
    if (!impl->identity.empty() && max_identity_len > 0) {
        const auto n = std::min(impl->identity.size(),
                                static_cast<std::size_t>(max_identity_len - 1));
        std::memcpy(identity_out, impl->identity.data(), n);
        identity_out[n] = '\0';
    } else if (max_identity_len > 0) {
        identity_out[0] = '\0';
    }
    if (impl->psk.size() > max_psk_len) return 0;
    std::memcpy(psk_out, impl->psk.data(), impl->psk.size());
    return static_cast<unsigned int>(impl->psk.size());
}

}  // namespace

TlsContext::TlsContext() noexcept = default;

TlsContext::~TlsContext() = default;

TlsContext::TlsContext(TlsContext&& other) noexcept = default;
TlsContext& TlsContext::operator=(TlsContext&& other) noexcept = default;

void* TlsContext::native_handle() const noexcept {
    return pimpl_ ? pimpl_->ctx : nullptr;
}

TlsContext::Mode TlsContext::mode() const noexcept {
    return pimpl_ ? pimpl_->mode : Mode::ServerPki;
}

std::unique_ptr<TlsContext>
TlsContext::create_psk(Mode mode,
                       std::span<const std::uint8_t> psk,
                       std::string_view identity) noexcept {
    if (psk.size() != 32) return nullptr;
    auto t = std::unique_ptr<TlsContext>(new TlsContext());
    t->pimpl_ = std::make_unique<Impl>(mode);
    t->pimpl_->psk.assign(psk.begin(), psk.end());
    t->pimpl_->identity = std::string(identity);

    const SSL_METHOD* method =
        (mode == Mode::ServerPsk) ? TLS_server_method() : TLS_client_method();
    t->pimpl_->ctx = SSL_CTX_new(method);
    if (!t->pimpl_->ctx) return nullptr;

    SSL_CTX_set_app_data(t->pimpl_->ctx, t->pimpl_.get());
    SSL_CTX_set_min_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);

    SSL_CTX_set_psk_server_callback(t->pimpl_->ctx, psk_server_cb);
    SSL_CTX_set_psk_client_callback(t->pimpl_->ctx, psk_client_cb);
    if (mode == Mode::ClientPsk) {
        SSL_CTX_use_psk_identity_hint(t->pimpl_->ctx,
                                       t->pimpl_->identity.c_str());
    }
    enable_tls13_groups(t->pimpl_->ctx);
    return t;
}

std::unique_ptr<TlsContext>
TlsContext::create_server_pki(const std::filesystem::path& cert_file,
                              const std::filesystem::path& key_file) noexcept {
    auto t = std::unique_ptr<TlsContext>(new TlsContext());
    t->pimpl_ = std::make_unique<Impl>(Mode::ServerPki);

    t->pimpl_->ctx = SSL_CTX_new(TLS_server_method());
    if (!t->pimpl_->ctx) return nullptr;
    SSL_CTX_set_min_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);
    if (SSL_CTX_use_certificate_file(t->pimpl_->ctx,
                                     cert_file.string().c_str(),
                                     SSL_FILETYPE_PEM) != 1) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error,
            "TLS server: failed to load certificate",
            core::ErrorCode::INTERNAL);
        return nullptr;
    }
    if (SSL_CTX_use_PrivateKey_file(t->pimpl_->ctx,
                                    key_file.string().c_str(),
                                    SSL_FILETYPE_PEM) != 1) {
        return nullptr;
    }
    if (SSL_CTX_check_private_key(t->pimpl_->ctx) != 1) {
        return nullptr;
    }
    enable_tls13_groups(t->pimpl_->ctx);
    return t;
}

std::unique_ptr<TlsContext>
TlsContext::create_client_pki(const std::filesystem::path& ca_file,
                              const std::filesystem::path& cert_file,
                              const std::filesystem::path& key_file) noexcept {
    auto t = std::unique_ptr<TlsContext>(new TlsContext());
    t->pimpl_ = std::make_unique<Impl>(Mode::ClientPki);

    t->pimpl_->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->pimpl_->ctx) return nullptr;
    SSL_CTX_set_min_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(t->pimpl_->ctx, TLS1_3_VERSION);
    SSL_CTX_set_verify(t->pimpl_->ctx, SSL_VERIFY_PEER, nullptr);

    if (!ca_file.empty()) {
        if (SSL_CTX_load_verify_locations(t->pimpl_->ctx,
                                          ca_file.string().c_str(),
                                          nullptr) != 1) {
            return nullptr;
        }
    }
    if (!cert_file.empty() && !key_file.empty()) {
        if (SSL_CTX_use_certificate_file(t->pimpl_->ctx,
                                         cert_file.string().c_str(),
                                         SSL_FILETYPE_PEM) != 1) return nullptr;
        if (SSL_CTX_use_PrivateKey_file(t->pimpl_->ctx,
                                        key_file.string().c_str(),
                                        SSL_FILETYPE_PEM) != 1) return nullptr;
    }
    enable_tls13_groups(t->pimpl_->ctx);
    return t;
}

}  // namespace udaf::crypto