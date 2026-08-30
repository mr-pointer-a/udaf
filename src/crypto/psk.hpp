// psk.hpp - PSK 模式（HKDF-SHA256 派生 + AES-256-GCM AEAD）
//
// 设计依据：ADR-007 §2.1 KDF 派生链 + §2.2 AdvertisementHeader 加密方式
//
// 接口：
//   - hkdf_sha256(salt, ikm, info, length) → Result<SecretKey>
//   - psk_derive_session_keys(psk, salt) → Result<DerivedKeys>
//   - psk_aead_encrypt(key, nonce, plaintext, aad) → Result<vector>
//   - psk_aead_decrypt(key, nonce, ciphertext, aad) → Result<vector>
//   - psk_handshake_client(psk) → AuthRequest
//   - psk_handshake_server(psk, request) → Result<AuthResponse>
//   - psk_handshake_finalize_client(psk, response, request) → Result<DerivedKeys>

#ifndef UDAF_CRYPTO_PSK_HPP
#define UDAF_CRYPTO_PSK_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "auth_types.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// HKDF-SHA256(salt, ikm, info, length) → okm
[[nodiscard]] core::Result<SecretKey>
hkdf_sha256(std::span<const std::uint8_t> salt,
            std::span<const std::uint8_t> ikm,
            std::string_view info,
            std::size_t out_length) noexcept;

/// 从 PSK（32 字节随机）+ salt 派生会话密钥对（enc_key + mac_key）。
[[nodiscard]] core::Result<DerivedKeys>
psk_derive_session_keys(std::span<const std::uint8_t> psk,
                        std::span<const std::uint8_t> salt) noexcept;

/// AES-256-GCM 加密。
/// @param key 32 字节对称密钥
/// @param nonce 12 字节随机 nonce（永不重用）
/// @param plaintext 明文
/// @param aad additional authenticated data（可选）
/// @return Ok(密文 = ciphertext || 16-byte tag)；Err(INVALID_ARG / INTERNAL)
[[nodiscard]] core::Result<std::vector<std::uint8_t>>
psk_aead_encrypt(std::span<const std::uint8_t> key,
                 std::span<const std::uint8_t, 12> nonce,
                 std::span<const std::uint8_t> plaintext,
                 std::span<const std::uint8_t> aad) noexcept;

/// AES-256-GCM 解密。
[[nodiscard]] core::Result<std::vector<std::uint8_t>>
psk_aead_decrypt(std::span<const std::uint8_t> key,
                 std::span<const std::uint8_t, 12> nonce,
                 std::span<const std::uint8_t> ciphertext,
                 std::span<const std::uint8_t> aad) noexcept;

// ---------- 握手辅助 ----------

/// 客户端：构造握手请求（client_random + salt + identity）
[[nodiscard]] AuthRequest psk_handshake_client_new(std::string_view identity) noexcept;

/// 服务端：处理请求 + 构造响应（server_random + salt + encrypted_session_key + nonce + mac）
[[nodiscard]] core::Result<AuthResponse>
psk_handshake_server_respond(std::span<const std::uint8_t> psk,
                             const AuthRequest& request) noexcept;

/// 客户端：处理响应 → 解出会话密钥
[[nodiscard]] core::Result<DerivedKeys>
psk_handshake_client_finalize(std::span<const std::uint8_t> psk,
                              const AuthRequest& request,
                              const AuthResponse& response) noexcept;

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_PSK_HPP