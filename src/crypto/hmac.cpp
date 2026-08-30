// hmac.cpp - HMAC-SHA256 实现（OpenSSL 3.0 EVP_MAC API）
#include "hmac.hpp"

#include "core/log/logger.hpp"

#include <openssl/evp.h>
#include <openssl/params.h>

#include <cstring>

namespace udaf::crypto {

namespace {

/// CRYPTO_memcmp：OpenSSL 的常数时间内存比较（防时序攻击）。
inline int constant_time_memcmp(const void* a, const void* b, std::size_t n) noexcept {
    const auto* pa = static_cast<const std::uint8_t*>(a);
    const auto* pb = static_cast<const std::uint8_t*>(b);
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff |= pa[i] ^ pb[i];
    }
    return diff;
}

/// 模块级缓存的 EVP_MAC 算法句柄（HMAC）。OpenSSL 3.0 的 EVP_MAC_fetch
/// 是较昂贵的全局算法查找；缓存可显著减少每次握手的 fetch 开销。
EVP_MAC* cached_hmac() noexcept {
    static EVP_MAC* m = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    return m;
}

}  // namespace

core::Result<std::vector<std::uint8_t>>
hmac_sha256(std::span<const std::uint8_t> key,
            std::span<const std::uint8_t> message) noexcept {
    if (key.empty()) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INVALID_ARG);
    }

    EVP_MAC* mac = cached_hmac();
    if (!mac) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    if (!ctx) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }

    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", digest, 0),
        OSSL_PARAM_construct_end()
    };

    std::vector<std::uint8_t> out(EVP_MAX_MD_SIZE);
    std::size_t out_len = 0;

    bool ok = EVP_MAC_init(ctx, key.data(), key.size(), params) == 1
           && EVP_MAC_update(ctx, message.data(), message.size()) == 1
           && EVP_MAC_final(ctx, out.data(), &out_len, out.size()) == 1;

    EVP_MAC_CTX_free(ctx);

    if (!ok) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error, "HMAC-SHA256 compute failed",
            core::ErrorCode::INTERNAL);
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    out.resize(out_len);
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<void>
hmac_sha256_verify(std::span<const std::uint8_t> key,
                   std::span<const std::uint8_t> message,
                   std::span<const std::uint8_t> expected) noexcept {
    if (expected.size() != 32) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    auto r = hmac_sha256(key, message);
    if (r.is_err()) {
        return core::Result<void>::err(r.error());
    }
    if (constant_time_memcmp(r.value().data(), expected.data(), 32) != 0) {
        return core::Result<void>::err(core::ErrorCode::CRYPTO_HMAC_MISMATCH);
    }
    return core::Result<void>::ok();
}

}  // namespace udaf::crypto