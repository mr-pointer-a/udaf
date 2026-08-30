// tls_context.hpp - TLS 1.3 上下文 PIMPL（持有 SSL_CTX*）
//
// 设计要点：
//   - 不暴露 OpenSSL 类型（CLAUDE.md §3.5：解耦第三方库）
//   - Rule of Five：禁止拷贝（SSL_CTX 不能共享所有权）
//   - 支持 PSK 模式与 PKI 模式工厂

#ifndef UDAF_CRYPTO_TLS_CONTEXT_HPP
#define UDAF_CRYPTO_TLS_CONTEXT_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "auth_types.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::crypto {

/// TLS 1.3 上下文（PIMPL）。构造后即可持有 SSL_CTX*，提供工厂方法。
class TlsContext {
public:
    enum class Mode {
        ServerPsk,    // 服务端 PSK
        ClientPsk,    // 客户端 PSK
        ServerPki,    // 服务端 PKI
        ClientPki,    // 客户端 PKI
    };

    TlsContext() noexcept;
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    TlsContext(TlsContext&& other) noexcept;
    TlsContext& operator=(TlsContext&& other) noexcept;

    /// 是否有效（构造或初始化成功）
    [[nodiscard]] bool is_valid() const noexcept { return pimpl_ != nullptr; }

    /// 返回初始化时使用的模式
    [[nodiscard]] Mode mode() const noexcept;

    /// PSK 工厂：使用预共享密钥创建上下文
    /// @param mode ServerPsk 或 ClientPsk
    /// @param psk 32 字节 PSK
    /// @param identity 客户端标识
    [[nodiscard]] static std::unique_ptr<TlsContext>
    create_psk(Mode mode,
               std::span<const std::uint8_t> psk,
               std::string_view identity) noexcept;

    /// PKI 工厂：使用证书+私钥创建服务端上下文
    [[nodiscard]] static std::unique_ptr<TlsContext>
    create_server_pki(const std::filesystem::path& cert_file,
                      const std::filesystem::path& key_file) noexcept;

    /// PKI 工厂：使用 CA 证书创建客户端上下文
    [[nodiscard]] static std::unique_ptr<TlsContext>
    create_client_pki(const std::filesystem::path& ca_file,
                      const std::filesystem::path& cert_file,
                      const std::filesystem::path& key_file) noexcept;

    /// 测试/内部访问：返回底层 SSL_CTX* 指针（不转移所有权）
    [[nodiscard]] void* native_handle() const noexcept;

public:
    /// PIMPL 实现类型（公开以便实现文件构造）
    struct Impl;

private:
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace udaf::crypto

#endif  // UDAF_CRYPTO_TLS_CONTEXT_HPP