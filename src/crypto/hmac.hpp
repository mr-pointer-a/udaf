// hmac.hpp - HMAC-SHA256 包装（OpenSSL 3.0 EVP API）
//
// 设计要点：
//   - 使用 EVP_MAC API（OpenSSL 3.0 推荐，替代废弃的 HMAC()）
//   - 单次 init/update/final API
//   - 不抛异常（CLAUDE.md §3.5）
//   - 线程安全：每次调用独立 EVP_MAC_CTX，无共享状态

#ifndef UDAF_CRYPTO_HMAC_HPP
#define UDAF_CRYPTO_HMAC_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// 计算 HMAC-SHA256(message, key) → 32 字节输出。
/// @param key 对称密钥（任意长度，> 32 字节则先 SHA-256）
/// @param message 待签消息
/// @return Ok(32 字节 MAC) / Err(INTERNAL / INVALID_ARG)
[[nodiscard]] core::Result<std::vector<std::uint8_t>>
hmac_sha256(std::span<const std::uint8_t> key,
            std::span<const std::uint8_t> message) noexcept;

/// HMAC-SHA256 验证（常数时间比较，防时序攻击）。
/// @param expected 期望的 32 字节 MAC
/// @return OK() / Err(CRYPTO_HMAC_MISMATCH)
[[nodiscard]] core::Result<void>
hmac_sha256_verify(std::span<const std::uint8_t> key,
                   std::span<const std::uint8_t> message,
                   std::span<const std::uint8_t> expected) noexcept;

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_HMAC_HPP