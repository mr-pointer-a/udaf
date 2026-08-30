// auth_types.hpp - 加密模块共享类型
#ifndef UDAF_CRYPTO_AUTH_TYPES_HPP
#define UDAF_CRYPTO_AUTH_TYPES_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// 认证模式（与 Config::NetworkMode 一致；此处独立以减少耦合）
enum class AuthMode : std::uint8_t {
    Psk = 0,
    Pki = 1,
};

/// 32 字节对称密钥类型
using SecretKey = std::vector<std::uint8_t>;

/// 12 字节 AEAD nonce
using Nonce = std::array<std::uint8_t, 12>;

/// 32 字节 HMAC 输出
using Mac = std::array<std::uint8_t, 32>;

/// 认证握手请求（PSK 模式）：客户端发 client_random + salt
struct AuthRequest {
    std::vector<std::uint8_t> client_random;   // 32 字节
    std::vector<std::uint8_t> salt;             // 32 字节
    std::vector<std::uint8_t> identity;         // 客户端标识（如 device_node_id）
};

/// 认证握手响应（PSK 模式）：服务端回 server_random + salt + 加密的 session_key
struct AuthResponse {
    std::vector<std::uint8_t> server_random;   // 32 字节
    std::vector<std::uint8_t> salt;             // 32 字节（独立）
    std::vector<std::uint8_t> encrypted_session_key;  // AEAD 密文
    Nonce                      nonce;           // 12 字节
    Mac                        mac;             // 32 字节
};

/// 派生密钥集合（HKDF 输出）
struct DerivedKeys {
    SecretKey enc_key;       // AES-256-GCM（32 字节）
    SecretKey mac_key;       // HMAC-SHA256（32 字节）
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_AUTH_TYPES_HPP