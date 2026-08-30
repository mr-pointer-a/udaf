// pki.cpp - PKI 握手 + TLS 1.3 加密/解密
#include "pki.hpp"

#include "core/log/logger.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <cstring>

namespace udaf::crypto {

namespace {

void* as_ssl(void* p) { return p; }

}  // namespace

struct PkiHandshakeDeleter {
    void operator()(PkiHandshake* p) const noexcept { delete p; }
};

std::unique_ptr<PkiHandshake>
PkiHandshake::create(std::unique_ptr<TlsContext> ctx, int fd) noexcept {
    if (!ctx || !ctx->is_valid() || fd < 0) return nullptr;
    auto h = std::unique_ptr<PkiHandshake>(new PkiHandshake());
    h->ctx_ = std::move(ctx);
    h->fd_ = fd;

    SSL* ssl = SSL_new(static_cast<SSL_CTX*>(h->ctx_->native_handle()));
    if (!ssl) return nullptr;
    h->ssl_ = as_ssl(ssl);

    SSL_set_fd(ssl, fd);
    // 自动 flush
    SSL_set_mode(ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    // 设置到服务端/客户端握手
    if (h->ctx_->mode() == TlsContext::Mode::ServerPki) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }
    h->stage_ = PkiHandshakeStage::Init;
    return h;
}

PkiHandshake::~PkiHandshake() {
    if (ssl_) SSL_free(static_cast<SSL*>(ssl_));
}

PkiHandshakeStage PkiHandshake::step() noexcept {
    if (stage_ == PkiHandshakeStage::Done || stage_ == PkiHandshakeStage::Failed) {
        return stage_;
    }
    SSL* ssl = static_cast<SSL*>(ssl_);
    int ret = (ctx_->mode() == TlsContext::Mode::ServerPki)
              ? SSL_accept(ssl)
              : SSL_connect(ssl);
    if (ret == 1) {
        stage_ = PkiHandshakeStage::Done;
        return stage_;
    }
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ) {
        stage_ = PkiHandshakeStage::WantsRead;
    } else if (err == SSL_ERROR_WANT_WRITE) {
        stage_ = PkiHandshakeStage::WantsWrite;
    } else {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error,
            "PKI handshake step failed",
            core::ErrorCode::INTERNAL);
        stage_ = PkiHandshakeStage::Failed;
    }
    return stage_;
}

core::Result<std::vector<std::uint8_t>>
PkiHandshake::peer_fingerprint() noexcept {
    if (stage_ != PkiHandshakeStage::Done) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    SSL* ssl = static_cast<SSL*>(ssl_);
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &digest_len) != 1) {
        X509_free(cert);
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    std::vector<std::uint8_t> out(digest, digest + digest_len);
    X509_free(cert);
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<std::vector<std::uint8_t>>
PkiHandshake::encrypt(std::span<const std::uint8_t> plaintext) noexcept {
    if (stage_ != PkiHandshakeStage::Done) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    SSL* ssl = static_cast<SSL*>(ssl_);
    std::vector<std::uint8_t> out(plaintext.size() + 32);
    int n = SSL_write(ssl, plaintext.data(), static_cast<int>(plaintext.size()));
    if (n <= 0) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    // TLS 1.3 会做自动 encrypt；返回的数据可被对端 SSL_read 读取
    out.resize(static_cast<std::size_t>(n));
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<std::vector<std::uint8_t>>
PkiHandshake::decrypt(std::span<const std::uint8_t> ciphertext) noexcept {
    if (stage_ != PkiHandshakeStage::Done) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    SSL* ssl = static_cast<SSL*>(ssl_);
    if (SSL_write(ssl, ciphertext.data(), static_cast<int>(ciphertext.size())) <= 0) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    std::vector<std::uint8_t> out(4096);
    int n = SSL_read(ssl, out.data(), static_cast<int>(out.size()));
    if (n < 0) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    out.resize(static_cast<std::size_t>(n));
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace udaf::crypto