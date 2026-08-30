# src/crypto 模块 - 阶段 B1（udaf::crypto）
# 详细设计见 docs/04-module-design.md §2.10 + docs/adr/ADR-004-auth-model.md
#
# 公开 API：
#   udaf::crypto::hmac_sha256 / hmac_sha256_verify     HMAC-SHA256（OpenSSL 3.0 EVP API）
#   udaf::crypto::hkdf_sha256 / psk_aead_*             HKDF-SHA256 + AES-256-GCM AEAD
#   udaf::crypto::TlsContext                          PIMPL 持有 SSL_CTX*
#   udaf::crypto::PkiHandshake                         TLS 1.3 完整握手驱动器
#   udaf::crypto::keystore / generate_psk              PSK 文件持久化（0640 权限）
#   udaf::crypto::Authenticator (接口)                 认证接口
#   udaf::crypto::PskAuthenticator                     PSK 实现
#   udaf::crypto::PkiAuthenticator                     PKI 实现

add_library(udaf_crypto STATIC
    hmac.cpp
    psk.cpp
    keystore.cpp
    tls_context.cpp
    pki.cpp
    psk_authenticator.cpp
    pki_authenticator.cpp
)

target_include_directories(udaf_crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(udaf_crypto
    PUBLIC
        udaf::udaf_core
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_compile_features(udaf_crypto PUBLIC cxx_std_20)

add_library(udaf::udaf_crypto ALIAS udaf_crypto)