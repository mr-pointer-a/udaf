// pki.hpp - PKI 模式：TLS 1.3 完整握手
//
// 设计依据：
//   - ADR-004 §2.3 PKI 模式：双向证书 + 双向 TLS 1.3 握手
//   - 性能契约（02 §3.4）：握手 P95 < 50ms
//   - 不抛异常（CLAUDE.md §3.5）

#ifndef UDAF_CRYPTO_PKI_HPP
#define UDAF_CRYPTO_PKI_HPP

#include <filesystem>
#include <memory>
#include <span>

#include "auth_types.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"
#include "tls_context.hpp"

namespace udaf::crypto {

/// PKI 握手阶段（用于 unit test 阶段化验证）
enum class PkiHandshakeStage : std::uint8_t {
    Init,
    WantsRead,    // 等待对端 TLS 字节
    WantsWrite,   // 等待把 TLS 字节发到对端
    Done,
    Failed,
};

/// PKI 握手驱动器：在一个 socket fd 上完成 TLS 1.3 双向握手。
/// 不拥有 fd 所有权，由调用方管理。
class PkiHandshake {
public:
    /// @param ctx 已配置好的 TlsContext（ServerPki 或 ClientPki）
    /// @param fd 已连接好的 socket fd（non-blocking 或 blocking 都行）
    static std::unique_ptr<PkiHandshake>
    create(std::unique_ptr<TlsContext> ctx, int fd) noexcept;

    ~PkiHandshake();

    PkiHandshake(const PkiHandshake&) = delete;
    PkiHandshake& operator=(const PkiHandshake&) = delete;
    PkiHandshake(PkiHandshake&&) = delete;
    PkiHandshake& operator=(PkiHandshake&&) = delete;

    /// 推进握手一步；可被多次调用直到 stage == Done 或 Failed。
    [[nodiscard]] PkiHandshakeStage step() noexcept;

    /// 当前阶段
    [[nodiscard]] PkiHandshakeStage stage() const noexcept { return stage_; }

    /// 握手是否完成
    [[nodiscard]] bool is_done() const noexcept { return stage_ == PkiHandshakeStage::Done; }

    /// 获取对端证书指纹（SHA-256, 32B）；握手完成后可用
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    peer_fingerprint() noexcept;

    /// 加密明文（握手完成后）
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    encrypt(std::span<const std::uint8_t> plaintext) noexcept;

    /// 解密密文（握手完成后）
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    decrypt(std::span<const std::uint8_t> ciphertext) noexcept;

private:
    PkiHandshake() = default;

    std::unique_ptr<TlsContext> ctx_;
    void* ssl_ = nullptr;            // SSL*
    int   fd_ = -1;
    PkiHandshakeStage stage_ = PkiHandshakeStage::Init;
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_PKI_HPP