// authenticator.hpp - Authenticator 抽象接口
//
// 设计依据（ADR-004）：
//   - PSK 模式：出厂预共享密钥，性能优先（P95 < 2ms）
//   - PKI 模式：完整 TLS 1.3 握手，安全优先（P95 < 50ms）
//   - 选择由 Config::CryptoConfig.mode 决定

#ifndef UDAF_CRYPTO_AUTHENTICATOR_HPP
#define UDAF_CRYPTO_AUTHENTICATOR_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "auth_types.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// Authenticator 抽象接口
class Authenticator {
public:
    virtual ~Authenticator() = default;

    /// 生成初始握手消息（客户端调用）
    [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>>
    begin_handshake() noexcept = 0;

    /// 处理对端消息并产生本端响应（双向调用）
    [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>>
    process_handshake(std::span<const std::uint8_t> peer_msg) noexcept = 0;

    /// 握手是否完成
    [[nodiscard]] virtual bool is_handshake_done() const noexcept = 0;

    /// 加密消息（handshake 完成后）
    [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>>
    encrypt(std::span<const std::uint8_t> plaintext) noexcept = 0;

    /// 解密消息（handshake 完成后）
    [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>>
    decrypt(std::span<const std::uint8_t> ciphertext) noexcept = 0;

    /// 派生出会话密钥（PSK 模式；PKI 模式返回 INTERNAL）
    [[nodiscard]] virtual core::Result<DerivedKeys>
    session_keys() noexcept = 0;
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_AUTHENTICATOR_HPP