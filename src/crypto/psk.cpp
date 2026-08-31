// psk.cpp - HKDF + AES-GCM + PSK 握手（OpenSSL 3.0 EVP API）
#include "psk.hpp"

#include "hmac.hpp"

#include "core/log/logger.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <cstring>
#include <utility>

namespace udaf::crypto {

namespace {

constexpr std::size_t kSessionKeyLen = 32;
constexpr const char* kSessionInfo   = "udaf-psk-v1-session";

/// 32 字节 CSPRNG
bool random_bytes(std::span<std::uint8_t> out) noexcept {
    return RAND_bytes(out.data(), static_cast<int>(out.size())) == 1;
}

/// 12 字节 nonce
Nonce random_nonce() noexcept {
    Nonce n{};
    random_bytes(n);
    return n;
}

/// 模块级缓存的 EVP_KDF 句柄（HKDF）。OpenSSL 3.0 的 EVP_KDF_fetch 是
/// 较昂贵的全局算法查找；缓存后每次握手节省约 100~200ns。
EVP_KDF* cached_kdf() noexcept {
    static EVP_KDF* k = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    return k;
}

}  // namespace

core::Result<SecretKey>
hkdf_sha256(std::span<const std::uint8_t> salt,
            std::span<const std::uint8_t> ikm,
            std::string_view info,
            std::size_t out_length) noexcept {
    if (out_length == 0 || out_length > static_cast<std::size_t>(255) * 32) {
        return core::Result<SecretKey>::err(core::ErrorCode::INVALID_ARG);
    }
    if (ikm.empty()) {
        return core::Result<SecretKey>::err(core::ErrorCode::INVALID_ARG);
    }

    EVP_KDF* kdf = cached_kdf();
    if (!kdf) {
        return core::Result<SecretKey>::err(core::ErrorCode::INTERNAL);
    }
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    if (!ctx) {
        return core::Result<SecretKey>::err(core::ErrorCode::INTERNAL);
    }

    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
            const_cast<std::uint8_t*>(salt.data()),
            salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
            const_cast<std::uint8_t*>(ikm.data()),
            ikm.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
            reinterpret_cast<unsigned char*>(const_cast<char*>(info.data())),
            info.size()),
        OSSL_PARAM_construct_end()
    };

    SecretKey okm(out_length);
    bool ok = EVP_KDF_derive(ctx, okm.data(), okm.size(), params) == 1;

    EVP_KDF_CTX_free(ctx);

    if (!ok) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error, "HKDF-SHA256 derive failed",
            core::ErrorCode::INTERNAL);
        return core::Result<SecretKey>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<SecretKey>::ok(std::move(okm));
}

core::Result<DerivedKeys>
psk_derive_session_keys(std::span<const std::uint8_t> psk,
                        std::span<const std::uint8_t> salt) noexcept {
    // PSK → session key (32B) → enc_key + mac_key
    auto session = hkdf_sha256(salt, psk, kSessionInfo, kSessionKeyLen);
    if (session.is_err()) {
        return core::Result<DerivedKeys>::err(session.error());
    }
    DerivedKeys dk;
    dk.enc_key = std::move(session).value();

    // 由 session key 派生独立 mac key（用相同 salt/info 加后缀）
    auto mac = hkdf_sha256(salt, dk.enc_key, "udaf-psk-v1-mac", kSessionKeyLen);
    if (mac.is_err()) {
        return core::Result<DerivedKeys>::err(mac.error());
    }
    dk.mac_key = std::move(mac).value();
    return core::Result<DerivedKeys>::ok(std::move(dk));
}

core::Result<std::vector<std::uint8_t>>
psk_aead_encrypt(std::span<const std::uint8_t> key,
                 std::span<const std::uint8_t, 12> nonce,
                 std::span<const std::uint8_t> plaintext,
                 std::span<const std::uint8_t> aad) noexcept {
    if (key.size() != 32) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INVALID_ARG);
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }

    bool ok = false;
    int out_len = 0, final_len = 0;
    std::vector<std::uint8_t> out(plaintext.size() + 16);  // 16B GCM tag

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) goto done;
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) goto done;
    if (!aad.empty()) {
        int tmp = 0;
        if (EVP_EncryptUpdate(ctx, nullptr, &tmp, aad.data(),
                               static_cast<int>(aad.size())) != 1) goto done;
    }
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx, out.data(), &out_len,
                              plaintext.data(),
                              static_cast<int>(plaintext.size())) != 1) goto done;
    }
    if (EVP_EncryptFinal_ex(ctx, out.data() + out_len, &final_len) != 1) goto done;

    // 追加 16B tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16,
                             out.data() + out_len + final_len) != 1) goto done;
    out.resize(static_cast<std::size_t>(out_len) +
               static_cast<std::size_t>(final_len) + 16U);
    ok = true;

done:
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<std::vector<std::uint8_t>>
psk_aead_decrypt(std::span<const std::uint8_t> key,
                 std::span<const std::uint8_t, 12> nonce,
                 std::span<const std::uint8_t> ciphertext,
                 std::span<const std::uint8_t> aad) noexcept {
    if (key.size() != 32 || ciphertext.size() < 16) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INVALID_ARG);
    }
    const std::size_t body = ciphertext.size() - 16;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INTERNAL);
    }

    bool ok = false;
    int out_len = 0, final_len = 0;
    std::vector<std::uint8_t> out(ciphertext.size());

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) goto done;
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) goto done;
    if (!aad.empty()) {
        int tmp = 0;
        if (EVP_DecryptUpdate(ctx, nullptr, &tmp, aad.data(),
                               static_cast<int>(aad.size())) != 1) goto done;
    }

    if (body > 0) {
        if (EVP_DecryptUpdate(ctx, out.data(), &out_len,
                              ciphertext.data(),
                              static_cast<int>(body)) != 1) goto done;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                             const_cast<std::uint8_t*>(ciphertext.data()) + body) != 1) goto done;
    if (EVP_DecryptFinal_ex(ctx, out.data() + out_len, &final_len) != 1) goto done;
    out.resize(static_cast<std::size_t>(out_len) +
               static_cast<std::size_t>(final_len));
    ok = true;

done:
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        // GCM tag 验证失败或内部错误
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::CRYPTO_HMAC_MISMATCH);
    }
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

// ---------- 握手辅助 ----------
// 简化的 PSK 握手协议：
//   Client → Server: AuthRequest{client_random(32), salt(32), identity}
//   Server → Client: AuthResponse{server_random(32), salt'(32),
//                                 encrypted_session_key, nonce(12), mac(32)}
//   Client 验证 mac + 解密得到 session_key。

AuthRequest psk_handshake_client_new(std::string_view identity) noexcept {
    AuthRequest req;
    req.client_random.assign(32, 0);
    random_bytes(req.client_random);
    req.salt.assign(32, 0);
    random_bytes(req.salt);
    req.identity.assign(identity.begin(), identity.end());
    return req;
}

core::Result<AuthResponse>
psk_handshake_server_respond(std::span<const std::uint8_t> psk,
                             const AuthRequest& request) noexcept {
    if (psk.size() != 32 || request.client_random.size() != 32 || request.salt.size() != 32) {
        return core::Result<AuthResponse>::err(core::ErrorCode::INVALID_ARG);
    }

    AuthResponse resp;
    resp.server_random.assign(32, 0);
    random_bytes(resp.server_random);
    resp.salt.assign(32, 0);
    random_bytes(resp.salt);
    resp.nonce = random_nonce();

    // 派生 server_session_key（用客户端的 salt）
    auto dk = psk_derive_session_keys(psk, request.salt);
    if (dk.is_err()) {
        return core::Result<AuthResponse>::err(dk.error());
    }

    // 用 server_session_key 加密 server_random 作为 session_key
    auto enc = psk_aead_encrypt(dk.value().enc_key, resp.nonce,
                                 resp.server_random, request.client_random);
    if (enc.is_err()) {
        return core::Result<AuthResponse>::err(enc.error());
    }
    resp.encrypted_session_key = std::move(enc).value();

    // mac = HMAC(mac_key, client_random || server_random || nonce || enc_data)
    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(request.client_random.size() + resp.server_random.size() +
                     resp.nonce.size() + resp.encrypted_session_key.size());
    mac_data.insert(mac_data.end(), request.client_random.begin(), request.client_random.end());
    mac_data.insert(mac_data.end(), resp.server_random.begin(), resp.server_random.end());
    mac_data.insert(mac_data.end(), resp.nonce.begin(), resp.nonce.end());
    mac_data.insert(mac_data.end(), resp.encrypted_session_key.begin(),
                    resp.encrypted_session_key.end());
    auto mac = hmac_sha256(dk.value().mac_key, mac_data);
    if (mac.is_err()) {
        return core::Result<AuthResponse>::err(mac.error());
    }
    if (mac.value().size() != 32) {
        return core::Result<AuthResponse>::err(core::ErrorCode::INTERNAL);
    }
    std::memcpy(resp.mac.data(), mac.value().data(), 32);
    return core::Result<AuthResponse>::ok(std::move(resp));
}

core::Result<DerivedKeys>
psk_handshake_client_finalize(std::span<const std::uint8_t> psk,
                              const AuthRequest& request,
                              const AuthResponse& response) noexcept {
    if (psk.size() != 32) {
        return core::Result<DerivedKeys>::err(core::ErrorCode::INVALID_ARG);
    }
    // 派生同样的 server_session_key
    auto dk = psk_derive_session_keys(psk, request.salt);
    if (dk.is_err()) {
        return core::Result<DerivedKeys>::err(dk.error());
    }

    // 验证 mac
    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(request.client_random.size() + response.server_random.size() +
                     response.nonce.size() + response.encrypted_session_key.size());
    mac_data.insert(mac_data.end(), request.client_random.begin(), request.client_random.end());
    mac_data.insert(mac_data.end(), response.server_random.begin(), response.server_random.end());
    mac_data.insert(mac_data.end(), response.nonce.begin(), response.nonce.end());
    mac_data.insert(mac_data.end(), response.encrypted_session_key.begin(),
                    response.encrypted_session_key.end());
    auto v = hmac_sha256_verify(dk.value().mac_key, mac_data, response.mac);
    if (v.is_err()) {
        return core::Result<DerivedKeys>::err(v.error());
    }

    // 解密 server_session_key；与 dk.enc_key 一致（用于验证一致性）
    auto dec = psk_aead_decrypt(dk.value().enc_key, response.nonce,
                                 response.encrypted_session_key, request.client_random);
    if (dec.is_err()) {
        return core::Result<DerivedKeys>::err(dec.error());
    }
    // 双方最终会话密钥 = server_session_key（= dk.enc_key）+ mac_key
    DerivedKeys out;
    out.enc_key = dk.value().enc_key;       // 客户端与服务端持一致
    out.mac_key = dk.value().mac_key;
    return core::Result<DerivedKeys>::ok(std::move(out));
}

}  // namespace udaf::crypto