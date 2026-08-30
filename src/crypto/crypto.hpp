// crypto.hpp - UDAF 加密模块统一头文件
//
// 模块组织：
//   - hmac.hpp            HMAC-SHA256（OpenSSL 3.0 EVP API）
//   - tls_context.hpp     TLS 1.3 PIMPL（持有 SSL_CTX*）
//   - psk.hpp             PSK 模式（HKDF-SHA256 + AES-256-GCM AEAD）
//   - pki.hpp             PKI 模式（TLS 1.3 完整握手）
//   - keystore.hpp        PSK 文件持久化（0750 权限）
//   - authenticator.hpp   Authenticator 抽象接口
//   - psk_authenticator.hpp / pki_authenticator.hpp  实现
//   - auth_types.hpp      共享枚举（Mode / AuthRequest / AuthResponse）
//
// 设计依据：
//   - docs/04-module-design.md §2.10
//   - docs/adr/ADR-004-auth-model.md（PSK vs PKI）
//   - docs/adr/ADR-007-psk-kdf.md（HKDF 派生链 + AEAD + 重放防护）
//   - CLAUDE.md §3.5（不抛异常）+ §3.1（明文密码禁传）

#ifndef UDAF_CRYPTO_CRYPTO_HPP
#define UDAF_CRYPTO_CRYPTO_HPP

#include "auth_types.hpp"
#include "authenticator.hpp"
#include "hmac.hpp"
#include "keystore.hpp"
#include "pki.hpp"
#include "pki_authenticator.hpp"
#include "psk.hpp"
#include "psk_authenticator.hpp"
#include "tls_context.hpp"

#endif  // UDAF_CRYPTO_CRYPTO_HPP