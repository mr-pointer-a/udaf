// keystore.hpp - PSK 文件持久化
//
// 设计要点（ADR-007 §2.5）：
//   - 文件路径默认 /etc/udaf/psk.bin，权限 0640
//   - 32 字节随机 PSK（≥256 bit 熵）
//   - 不存明文：当前版本存 PSK 本身（未来版本用 Argon2id 主密钥加密）
//   - 不抛异常

#ifndef UDAF_CRYPTO_KEYSTORE_HPP
#define UDAF_CRYPTO_KEYSTORE_HPP

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "auth_types.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// 生成 32 字节随机 PSK。
[[nodiscard]] core::Result<SecretKey> generate_psk() noexcept;

/// 加载文件中的 PSK（自动校验长度 32 字节 + magic）。
[[nodiscard]] core::Result<SecretKey>
load_psk_from_file(const std::filesystem::path& path) noexcept;

/// 保存 PSK 到文件（带 magic + 权限 0640）。
[[nodiscard]] core::Result<void>
save_psk_to_file(const std::filesystem::path& path, std::span<const std::uint8_t> psk) noexcept;

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_KEYSTORE_HPP