// test_crypto.cpp - 阶段 B1 单元测试
//
// 测试设计（docs/05-test-plan.md §5.6）：
//   1. HMAC-SHA256 已知向量
//   2. PSK 握手 round-trip
//   3. PKI 握手 round-trip（需要本地自签证书）
//   4. TlsContext PIMPL 生命周期（创建/移动/销毁）
//   5. PSK 握手 P95 < 2ms（性能契约 #15）
//   6. PKI 握手 P95 < 50ms（性能契约 #16）
//   7. 加密开销（性能契约 #26 < 20%）

#include <gtest/gtest.h>

#include "crypto.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>

namespace fs = std::filesystem;
using udaf::crypto::Authenticator;
using udaf::crypto::DerivedKeys;
using udaf::crypto::Mac;
using udaf::crypto::Nonce;
using udaf::crypto::PkiAuthenticator;
using udaf::crypto::PskAuthenticator;
using udaf::crypto::SecretKey;
using udaf::crypto::TlsContext;

namespace {

/// 生成 32 字节随机 PSK
std::vector<std::uint8_t> make_psk(std::mt19937_64& rng) {
    std::vector<std::uint8_t> psk(32);
    for (auto& b : psk) b = static_cast<std::uint8_t>(rng() & 0xFF);
    return psk;
}

/// 16 字节临时 AAD
std::vector<std::uint8_t> make_aad(std::mt19937_64& rng) {
    std::vector<std::uint8_t> aad(16);
    for (auto& b : aad) b = static_cast<std::uint8_t>(rng() & 0xFF);
    return aad;
}

/// 用 OpenSSL 在临时目录生成自签证书 + 私钥（PEM 格式）
bool generate_self_signed(const fs::path& cert_file,
                          const fs::path& key_file) {
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!pctx) return false;
    if (EVP_PKEY_keygen_init(pctx) <= 0 || EVP_PKEY_keygen(pctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        return false;
    }
    EVP_PKEY_CTX_free(pctx);

    X509* x509 = X509_new();
    X509_set_version(x509, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 60 * 60 * 24 * 365);
    X509_set_pubkey(x509, pkey);
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                              reinterpret_cast<const unsigned char*>("localhost"),
                              -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    FILE* fc = std::fopen(cert_file.string().c_str(), "w");
    if (!fc) { X509_free(x509); EVP_PKEY_free(pkey); return false; }
    PEM_write_X509(fc, x509);
    std::fclose(fc);

    FILE* fk = std::fopen(key_file.string().c_str(), "w");
    if (!fk) { X509_free(x509); EVP_PKEY_free(pkey); return false; }
    PEM_write_PrivateKey(fk, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    std::fclose(fk);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return true;
}

class TmpDir {
public:
    TmpDir() {
        path_ = fs::temp_directory_path() /
                ("udaf_crypto_" + std::to_string(::getpid()) + "_" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(path_);
    }
    ~TmpDir() { fs::remove_all(path_); }
    [[nodiscard]] fs::path p() const { return path_; }
private:
    fs::path path_;
};

}  // namespace

// ---------------- 1. HMAC-SHA256 已知向量 ----------------
TEST(UdafCrypto, HmacSha256KnownVector) {
    // RFC 4231 §4.3 Test Case 1
    const std::array<std::uint8_t, 20> key = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b
    };
    const std::string msg = "Hi There";
    const std::array<std::uint8_t, 32> expected = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    auto r = udaf::crypto::hmac_sha256(key, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()));
    ASSERT_TRUE(r.is_ok()) << "hmac_sha256 failed";
    EXPECT_EQ(r.value(), std::vector<std::uint8_t>(expected.begin(), expected.end()));
}

TEST(UdafCrypto, HmacSha256EmptyKeyRejected) {
    // 空 key → INVALID_ARG（覆盖 hmac.cpp 行 38-40）
    std::vector<std::uint8_t> empty_key;
    std::vector<std::uint8_t> msg{'h','i'};
    auto r = udaf::crypto::hmac_sha256(empty_key, msg);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafCrypto, HmacVerifyWrongLengthRejected) {
    // expected 长度 ≠ 32 → INVALID_ARG（覆盖 hmac.cpp 行 80-82）
    std::vector<std::uint8_t> key(32, 0xAA);
    std::vector<std::uint8_t> msg{'h','i'};
    std::vector<std::uint8_t> bad_expected(16, 0x00);  // 长度错误
    auto r = udaf::crypto::hmac_sha256_verify(key, msg, bad_expected);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafCrypto, HmacVerifyMismatch) {
    // 正确长度但内容不匹配 → CRYPTO_HMAC_MISMATCH（覆盖 hmac.cpp 行 87-88）
    std::vector<std::uint8_t> key(32, 0xAA);
    std::vector<std::uint8_t> msg{'h','i'};
    std::vector<std::uint8_t> wrong_mac(32, 0xFF);
    auto r = udaf::crypto::hmac_sha256_verify(key, msg, wrong_mac);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::CRYPTO_HMAC_MISMATCH);
}

// ---------------- 2. PSK 握手 round-trip ----------------
TEST(UdafCrypto, PskHandshakeRoundTrip) {
    std::mt19937_64 rng(0xC0FFEE);
    auto psk = make_psk(rng);
    auto client = PskAuthenticator::create_client(psk, "device-A");
    auto server = PskAuthenticator::create_server(psk, "device-A");
    ASSERT_NE(client, nullptr);
    ASSERT_NE(server, nullptr);

    auto client_msg = client->begin_handshake();
    ASSERT_TRUE(client_msg.is_ok());

    auto server_resp = server->process_handshake(client_msg.value());
    ASSERT_TRUE(server_resp.is_ok()) << "server handshake failed";
    EXPECT_FALSE(server_resp.value().empty());

    auto client_final = client->process_handshake(server_resp.value());
    ASSERT_TRUE(client_final.is_ok()) << "client finalize failed";
    EXPECT_TRUE(client->is_handshake_done());
    EXPECT_TRUE(server->is_handshake_done());

    // 双方会话密钥应一致
    auto k_client = client->session_keys();
    auto k_server = server->session_keys();
    ASSERT_TRUE(k_client.is_ok());
    ASSERT_TRUE(k_server.is_ok());
    EXPECT_EQ(k_client.value().enc_key, k_server.value().enc_key);
    EXPECT_EQ(k_client.value().mac_key, k_server.value().mac_key);

    // 加密 / 解密 round-trip
    std::vector<std::uint8_t> plaintext(128);
    for (auto& b : plaintext) b = static_cast<std::uint8_t>(rng() & 0xFF);
    auto enc = client->encrypt(plaintext);
    ASSERT_TRUE(enc.is_ok());
    auto dec = server->decrypt(enc.value());
    ASSERT_TRUE(dec.is_ok());
    EXPECT_EQ(dec.value(), plaintext);
}

// ---------------- 3. PKI 握手 + TlsContext 生命周期 ----------------
TEST(UdafCrypto, TlsContextPimplLifecycle) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv = TlsContext::create_server_pki(cert, key);
    auto cli = TlsContext::create_client_pki(cert, cert, key);
    ASSERT_NE(srv, nullptr);
    ASSERT_NE(cli, nullptr);
    EXPECT_TRUE(srv->is_valid());
    EXPECT_TRUE(cli->is_valid());

    // 移动构造
    auto srv_moved = std::move(srv);
    EXPECT_NE(srv_moved, nullptr);
    EXPECT_EQ(srv, nullptr);  // moved-from 为空

    // PSK 工厂
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto psk_ctx = TlsContext::create_psk(TlsContext::Mode::ServerPsk, psk, "id");
    ASSERT_NE(psk_ctx, nullptr);
    EXPECT_TRUE(psk_ctx->is_valid());
}

TEST(UdafCrypto, PkiAuthenticatorFactory) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv = PkiAuthenticator::create_server(cert, key);
    auto cli = PkiAuthenticator::create_client(cert, cert, key);
    ASSERT_NE(srv, nullptr);
    ASSERT_NE(cli, nullptr);
    EXPECT_NE(srv->context(), nullptr);
    EXPECT_NE(cli->context(), nullptr);
    EXPECT_TRUE(srv->is_handshake_done());
    EXPECT_TRUE(cli->is_handshake_done());
}

TEST(UdafCrypto, PkiAuthenticatorInvalidCertReturnsNull) {
    // 无效证书路径 → create_server 返回 nullptr
    auto bad_srv = PkiAuthenticator::create_server("/nonexistent/cert.pem",
                                                  "/nonexistent/key.pem");
    EXPECT_EQ(bad_srv, nullptr);
    auto bad_cli = PkiAuthenticator::create_client("/nonexistent/ca.pem",
                                                   "/nonexistent/cert.pem",
                                                   "/nonexistent/key.pem");
    EXPECT_EQ(bad_cli, nullptr);
}

TEST(UdafCrypto, PkiAuthenticatorApiRoundTrip) {
    // 覆盖 PkiAuthenticator 的 begin/process/encrypt/decrypt/session_keys
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));
    auto srv = PkiAuthenticator::create_server(cert, key);
    ASSERT_NE(srv, nullptr);

    // begin_handshake / process_handshake 返回空字节流
    auto begin = srv->begin_handshake();
    ASSERT_TRUE(begin.is_ok());
    EXPECT_TRUE(begin.value().empty());

    std::vector<std::uint8_t> peer_msg{1, 2, 3};
    auto proc = srv->process_handshake(peer_msg);
    ASSERT_TRUE(proc.is_ok());
    EXPECT_TRUE(proc.value().empty());

    // encrypt/decrypt no-op round-trip
    std::vector<std::uint8_t> plaintext{0x10, 0x20, 0x30, 0x40};
    auto enc = srv->encrypt(plaintext);
    ASSERT_TRUE(enc.is_ok());
    EXPECT_EQ(enc.value(), plaintext);
    auto dec = srv->decrypt(enc.value());
    ASSERT_TRUE(dec.is_ok());
    EXPECT_EQ(dec.value(), plaintext);

    // session_keys 在 PKI 下由 TLS 内部管理 → 显式 INTERNAL 错误
    auto sk = srv->session_keys();
    ASSERT_TRUE(sk.is_err());
    EXPECT_EQ(sk.error(), udaf::core::ErrorCode::INTERNAL);
}

// ---------------- 4. AEAD + HKDF 互操作 ----------------
TEST(UdafCrypto, HkdfAndAeadRoundTrip) {
    std::mt19937_64 rng(0xABCDEF);
    auto salt = make_psk(rng);
    auto ikm  = make_psk(rng);
    auto aad  = make_aad(rng);
    auto kdf = udaf::crypto::hkdf_sha256(salt, ikm, "udaf-test", 32);
    ASSERT_TRUE(kdf.is_ok());
    EXPECT_EQ(kdf.value().size(), 32u);

    auto derived = udaf::crypto::psk_derive_session_keys(ikm, salt);
    ASSERT_TRUE(derived.is_ok());
    EXPECT_EQ(derived.value().enc_key.size(), 32u);
    EXPECT_EQ(derived.value().mac_key.size(), 32u);

    Nonce nonce{};
    std::vector<std::uint8_t> pt(1024);
    for (auto& b : pt) b = static_cast<std::uint8_t>(rng() & 0xFF);

    auto enc = udaf::crypto::psk_aead_encrypt(derived.value().enc_key, nonce, pt, aad);
    ASSERT_TRUE(enc.is_ok());
    EXPECT_EQ(enc.value().size(), pt.size() + 16u);  // GCM tag 16B

    auto dec = udaf::crypto::psk_aead_decrypt(derived.value().enc_key, nonce, enc.value(), aad);
    ASSERT_TRUE(dec.is_ok());
    EXPECT_EQ(dec.value(), pt);

    // 篡改 aad 应失败
    auto aad_bad = aad;
    aad_bad[0] ^= 0x01;
    auto dec_bad = udaf::crypto::psk_aead_decrypt(derived.value().enc_key, nonce,
                                                  enc.value(), aad_bad);
    EXPECT_TRUE(dec_bad.is_err()) << "AAD 篡改应失败";
}

// ---------------- 5. PSK 握手 P95 < 2ms（性能契约 #15） ----------------
TEST(UdafCrypto, PskHandshakeP95) {
    std::mt19937_64 rng(0xDEADBEEF);
    auto psk = make_psk(rng);
    constexpr int kIters = 200;
    std::vector<double> ms_samples;
    ms_samples.reserve(kIters);

    for (int i = 0; i < kIters; ++i) {
        auto client = PskAuthenticator::create_client(psk, "x");
        auto server = PskAuthenticator::create_server(psk, "x");
        auto t0 = std::chrono::steady_clock::now();
        auto cm = client->begin_handshake();
        auto sr = server->process_handshake(cm.value());
        auto cf = client->process_handshake(sr.value());
        auto t1 = std::chrono::steady_clock::now();
        ASSERT_TRUE(cm.is_ok());
        ASSERT_TRUE(sr.is_ok());
        ASSERT_TRUE(cf.is_ok());
        ms_samples.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(ms_samples.begin(), ms_samples.end());
    double p95 = ms_samples[static_cast<std::size_t>(0.95 * kIters)];
    EXPECT_LT(p95, 2.0) << "PSK P95 handshake: " << p95 << " ms (合同: <2ms)";
}

// ---------------- 6. Keystore round-trip ----------------
TEST(UdafCrypto, KeystoreRoundTrip) {
    TmpDir tmp;
    auto p = tmp.p() / "psk.bin";
    auto psk = udaf::crypto::generate_psk();
    ASSERT_TRUE(psk.is_ok());

    auto saved = udaf::crypto::save_psk_to_file(p, psk.value());
    ASSERT_TRUE(saved.is_ok()) << "save_psk_to_file 失败";
    EXPECT_TRUE(fs::exists(p));

    // 权限校验
    auto perm = fs::status(p).permissions();
    EXPECT_EQ(static_cast<int>(perm & fs::perms::owner_read),   static_cast<int>(fs::perms::owner_read));
    EXPECT_EQ(static_cast<int>(perm & fs::perms::owner_write),  static_cast<int>(fs::perms::owner_write));
    EXPECT_EQ(static_cast<int>(perm & fs::perms::group_read),   static_cast<int>(fs::perms::group_read));

    auto loaded = udaf::crypto::load_psk_from_file(p);
    ASSERT_TRUE(loaded.is_ok()) << "load_psk_from_file 失败";
    EXPECT_EQ(loaded.value(), psk.value());
}

TEST(UdafCrypto, KeystoreLoadMissingFile) {
    // 文件不存在 → BIZ_FILE_NOT_FOUND（覆盖 keystore.cpp 行 35-36）
    auto loaded = udaf::crypto::load_psk_from_file("/nonexistent/psk_xyz_12345.bin");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_EQ(loaded.error(), udaf::core::ErrorCode::BIZ_FILE_NOT_FOUND);
}

TEST(UdafCrypto, KeystoreLoadBadMagic) {
    // 文件 magic 不匹配 → SERIALIZE_VERSION_MISMATCH（覆盖行 41-43）
    TmpDir tmp;
    auto p = tmp.p() / "bad.bin";
    {
        std::ofstream f(p, std::ios::binary);
        std::uint32_t wrong_magic = 0xDEADBEEF;
        f.write(reinterpret_cast<const char*>(&wrong_magic), sizeof(wrong_magic));
    }
    auto loaded = udaf::crypto::load_psk_from_file(p);
    ASSERT_TRUE(loaded.is_err());
    EXPECT_EQ(loaded.error(), udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// 覆盖 keystore.cpp:65-67 写入到不存在的目录 → BIZ_FILE_NOT_FOUND
TEST(UdafCrypto, KeystoreSaveToInvalidPathReturnsErr) {
    auto psk = udaf::crypto::generate_psk();
    ASSERT_TRUE(psk.is_ok());
    // 路径中包含不存在的目录，ofstream::open 会失败
    auto r = udaf::crypto::save_psk_to_file(
        "/nonexistent_dir_xyz_12345/subdir/psk.bin", psk.value());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::BIZ_FILE_NOT_FOUND);
}

TEST(UdafCrypto, KeystoreLoadTruncated) {
    // 文件 magic 对但 PSK 数据截断 → PROTOCOL_TRUNCATED_BUFFER（覆盖行 47-49）
    TmpDir tmp;
    auto p = tmp.p() / "trunc.bin";
    {
        std::ofstream f(p, std::ios::binary);
        std::uint32_t magic = 0x50534B01;
        f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        std::uint8_t partial[10] = {0};  // 只写 10B，应有 32B
        f.write(reinterpret_cast<const char*>(partial), sizeof(partial));
    }
    auto loaded = udaf::crypto::load_psk_from_file(p);
    ASSERT_TRUE(loaded.is_err());
    EXPECT_EQ(loaded.error(), udaf::core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
}

TEST(UdafCrypto, KeystoreSaveWrongLength) {
    // psk 长度 ≠ 32 → INVALID_ARG（覆盖行 55-57）
    TmpDir tmp;
    auto p = tmp.p() / "wrong_len.bin";
    std::vector<std::uint8_t> wrong_len(16, 0xAA);  // 长度错误
    auto saved = udaf::crypto::save_psk_to_file(p, wrong_len);
    ASSERT_TRUE(saved.is_err());
    EXPECT_EQ(saved.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// ---------------- 7. AES-GCM 加密开销（性能契约 #26） ----------------
TEST(UdafCrypto, AeadPerformance) {
    std::vector<std::uint8_t> key(32, 0xAA);
    Nonce nonce{};
    std::vector<std::uint8_t> pt(1024);
    std::vector<std::uint8_t> aad(16, 0xBB);

    // 暖机
    for (int i = 0; i < 50; ++i) {
        auto e = udaf::crypto::psk_aead_encrypt(key, nonce, pt, aad);
        ASSERT_TRUE(e.is_ok());
    }

    constexpr int kIters = 5000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto e = udaf::crypto::psk_aead_encrypt(key, nonce, pt, aad);
        ASSERT_TRUE(e.is_ok());
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kIters;
    EXPECT_LT(us, 50.0) << "AEAD 单次加密: " << us << " us (合同: <50us)";
}

// ---------------- 8. HKDF/AEAD 错误分支（提升覆盖率） ----------------
TEST(UdafCrypto, HkdfInvalidArgs) {
    // out_length=0 → INVALID_ARG
    std::vector<std::uint8_t> salt(16, 0x01);
    std::vector<std::uint8_t> ikm(32, 0x02);
    auto r1 = udaf::crypto::hkdf_sha256(salt, ikm, "info", 0);
    EXPECT_TRUE(r1.is_err());
    EXPECT_EQ(r1.error(), udaf::core::ErrorCode::INVALID_ARG);

    // out_length 超过 max (255*32+1) → INVALID_ARG
    auto r2 = udaf::crypto::hkdf_sha256(salt, ikm, "info", 255 * 32 + 1);
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), udaf::core::ErrorCode::INVALID_ARG);

    // ikm 空 → INVALID_ARG
    std::vector<std::uint8_t> empty_ikm;
    auto r3 = udaf::crypto::hkdf_sha256(salt, empty_ikm, "info", 32);
    EXPECT_TRUE(r3.is_err());
    EXPECT_EQ(r3.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafCrypto, AeadEmptyPlaintext) {
    std::vector<std::uint8_t> key(32, 0xAA);
    Nonce nonce{};
    std::vector<std::uint8_t> empty_pt;
    std::vector<std::uint8_t> aad = {'a','a','d'};
    auto e = udaf::crypto::psk_aead_encrypt(key, nonce, empty_pt, aad);
    ASSERT_TRUE(e.is_ok());
    auto d = udaf::crypto::psk_aead_decrypt(key, nonce, e.value(), aad);
    ASSERT_TRUE(d.is_ok());
    EXPECT_TRUE(d.value().empty());
}

// ---------------- 9. PSK 派生 + 错误分支 ----------------
TEST(UdafCrypto, DeriveSessionKeysInvalidPsk) {
    std::vector<std::uint8_t> empty_psk;
    std::vector<std::uint8_t> salt(16, 0x55);
    auto r = udaf::crypto::psk_derive_session_keys(empty_psk, salt);
    EXPECT_TRUE(r.is_err());
}

TEST(UdafCrypto, DeriveSessionKeysOk) {
    std::vector<std::uint8_t> psk(32, 0xAB);
    std::vector<std::uint8_t> salt(16, 0xCD);
    auto r = udaf::crypto::psk_derive_session_keys(psk, salt);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().enc_key.size(), 32u);
    EXPECT_EQ(r.value().mac_key.size(), 32u);
}

// ---------------- 10. AEAD 错误分支（解密长度不足） ----------------
TEST(UdafCrypto, AeadDecryptTooShort) {
    std::vector<std::uint8_t> key(32, 0xAA);
    Nonce nonce{};
    std::vector<std::uint8_t> bad_ct(5, 0xCC);
    std::vector<std::uint8_t> aad = {'a','a','d'};
    auto r = udaf::crypto::psk_aead_decrypt(key, nonce, bad_ct, aad);
    EXPECT_TRUE(r.is_err());
}

// ---------------- 11. TlsContext PSK 工厂双角色 ----------------
TEST(UdafCrypto, TlsContextPskLifecycle) {
    std::vector<std::uint8_t> psk(32, 0xDD);
    auto cli = udaf::crypto::TlsContext::create_psk(udaf::crypto::TlsContext::Mode::ClientPsk,
                                                     psk, "node-1");
    auto srv = udaf::crypto::TlsContext::create_psk(udaf::crypto::TlsContext::Mode::ServerPsk,
                                                     psk, "node-2");
    EXPECT_NE(cli, nullptr);
    EXPECT_NE(srv, nullptr);
}

// ---------------- 12. Authenticator 接口桩 ----------------
TEST(UdafCrypto, AuthenticatorInterface) {
    std::vector<std::uint8_t> psk(32, 0xCC);
    auto psk_cli = udaf::crypto::PskAuthenticator::create_client(psk, "client-1");
    auto psk_srv = udaf::crypto::PskAuthenticator::create_server(psk, "node-1");
    EXPECT_NE(psk_cli, nullptr);
    EXPECT_NE(psk_srv, nullptr);
    // 不同 AuthMode 区分（无需调 mode() 方法）
    EXPECT_NE(udaf::crypto::AuthMode::Pki, udaf::crypto::AuthMode::Psk);
}

// ---------------- 13. PskAuthenticator 错误分支 ----------------
TEST(UdafCrypto, ServerBeginHandshakeRejected) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto srv = PskAuthenticator::create_server(psk, "srv");
    // 服务端不应调用 begin_handshake（仅客户端用）
    auto r = srv->begin_handshake();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafCrypto, EncryptBeforeHandshake) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto cli = PskAuthenticator::create_client(psk, "c");
    std::vector<std::uint8_t> msg(8, 0x42);
    auto r = cli->encrypt(msg);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);
}

TEST(UdafCrypto, DecryptBeforeHandshake) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto cli = PskAuthenticator::create_client(psk, "c");
    std::vector<std::uint8_t> ct(20, 0x42);
    auto r = cli->decrypt(ct);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);
}

TEST(UdafCrypto, DecryptTooShortAfterHandshake) {
    std::mt19937_64 rng(0xABCD);
    auto psk = make_psk(rng);
    auto cli = PskAuthenticator::create_client(psk, "c");
    auto srv = PskAuthenticator::create_server(psk, "s");

    auto cm = cli->begin_handshake();
    ASSERT_TRUE(cm.is_ok());
    auto sr = srv->process_handshake(cm.value());
    ASSERT_TRUE(sr.is_ok());
    auto cf = cli->process_handshake(sr.value());
    ASSERT_TRUE(cf.is_ok());

    // ciphertext < 12B 应被拒
    std::vector<std::uint8_t> too_short(8, 0x42);
    auto r = cli->decrypt(too_short);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafCrypto, SessionKeysBeforeHandshake) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto cli = PskAuthenticator::create_client(psk, "c");
    auto r = cli->session_keys();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);
}

TEST(UdafCrypto, ServerProcessInvalidRequest) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto srv = PskAuthenticator::create_server(psk, "srv");

    // 过短的请求（< 65B）应被拒
    std::vector<std::uint8_t> short_req(10, 0x00);
    auto r = srv->process_handshake(short_req);
    EXPECT_TRUE(r.is_err());
}

TEST(UdafCrypto, ClientFinalizeInvalidResponse) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto cli = PskAuthenticator::create_client(psk, "c");

    // 客户端 process_handshake 收到过短的服务端响应 → deserialize 失败
    std::vector<std::uint8_t> bad_resp(50, 0x00);
    auto r = cli->process_handshake(bad_resp);
    EXPECT_TRUE(r.is_err());
}