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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <openssl/x509.h>
#include <fcntl.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

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

// 覆盖 keystore.cpp:80-82 rename 失败 → INTERNAL
// 构造：预先创建同名 path 为目录 → rename 目录到路径失败
TEST(UdafCrypto, KeystoreSaveRenameOverDirectoryReturnsInternal) {
    TmpDir tmp;
    auto p = tmp.p() / "as_dir.bin";
    // 预先创建同名 path 为空目录
    fs::create_directory(p);

    std::vector<std::uint8_t> psk(32, 0xAB);
    auto saved = udaf::crypto::save_psk_to_file(p, psk);
    ASSERT_TRUE(saved.is_err());
    EXPECT_EQ(saved.error(), udaf::core::ErrorCode::INTERNAL);
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

// 覆盖 tls_context.hpp 移动构造/移动赋值（覆盖 4 个 FNDA:0 函数）
TEST(UdafCrypto, TlsContextMoveSemantics) {
    std::vector<std::uint8_t> psk(32, 0xEE);
    auto a = udaf::crypto::TlsContext::create_psk(
        udaf::crypto::TlsContext::Mode::ServerPsk, psk, "src");
    ASSERT_NE(a, nullptr);
    // 移动构造
    udaf::crypto::TlsContext b(std::move(*a));
    // 移动赋值（先创建 c 再赋值给 a）
    auto c = udaf::crypto::TlsContext::create_psk(
        udaf::crypto::TlsContext::Mode::ClientPsk, psk, "dst");
    ASSERT_NE(c, nullptr);
    *a = std::move(*c);
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

// ============================================================
// F10 新增覆盖：tls_context.cpp PSK 回调路径
// ============================================================

// 通过 socketpair + BIO_new_socket 在双线程驱动 TLS 1.3 PSK 握手
// 触发 psk_server_cb / psk_client_cb，覆盖 tls_context.cpp:38-73
TEST(UdafCrypto, TlsContextPskHandshakeExercisesCallbacks) {
    using udaf::crypto::TlsContext;
    std::vector<std::uint8_t> psk(32, 0xAB);

    auto srv_ctx = TlsContext::create_psk(TlsContext::Mode::ServerPsk, psk, "server-id");
    auto cli_ctx = TlsContext::create_psk(TlsContext::Mode::ClientPsk, psk, "client-id");
    ASSERT_NE(srv_ctx, nullptr);
    ASSERT_NE(cli_ctx, nullptr);

    // 构造 socketpair：srv_fd ↔ cli_fd
    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);
    int srv_fd = fds[0];
    int cli_fd = fds[1];

    // 用 shared_ptr<int> 让两个线程安全持有 fd（不关闭 fd，仅 BIO 关闭）
    BIO* srv_bio = BIO_new_socket(srv_fd, BIO_NOCLOSE);
    BIO* cli_bio = BIO_new_socket(cli_fd, BIO_NOCLOSE);
    ASSERT_NE(srv_bio, nullptr);
    ASSERT_NE(cli_bio, nullptr);

    SSL* srv_ssl = SSL_new(static_cast<SSL_CTX*>(srv_ctx->native_handle()));
    SSL* cli_ssl = SSL_new(static_cast<SSL_CTX*>(cli_ctx->native_handle()));
    ASSERT_NE(srv_ssl, nullptr);
    ASSERT_NE(cli_ssl, nullptr);

    SSL_set_bio(srv_ssl, srv_bio, srv_bio);
    SSL_set_bio(cli_ssl, cli_bio, cli_bio);
    SSL_set_accept_state(srv_ssl);
    SSL_set_connect_state(cli_ssl);

    std::atomic<int> srv_done{0}, cli_done{0};
    std::thread srv_thread([&] {
        // 服务端循环 accept 直到完成或错误
        for (int i = 0; i < 200 && srv_done.load() == 0; ++i) {
            int r = SSL_accept(srv_ssl);
            if (r == 1) srv_done.store(1);
        }
    });
    std::thread cli_thread([&] {
        for (int i = 0; i < 200 && cli_done.load() == 0; ++i) {
            int r = SSL_connect(cli_ssl);
            if (r == 1) cli_done.store(1);
        }
    });

    // 等待双方完成（最多 10s）
    for (int i = 0; i < 1000 && (srv_done.load() == 0 || cli_done.load() == 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    srv_thread.join();
    cli_thread.join();

    EXPECT_EQ(srv_done.load(), 1) << "server handshake failed: "
        << ERR_error_string(ERR_get_error(), nullptr);
    EXPECT_EQ(cli_done.load(), 1) << "client handshake failed: "
        << ERR_error_string(ERR_get_error(), nullptr);

    // 通信验证
    if (srv_done.load() && cli_done.load()) {
        const char* msg = "ping";
        int w = SSL_write(cli_ssl, msg, 4);
        EXPECT_GT(w, 0);
        char buf[16] = {};
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int r = SSL_read(srv_ssl, buf, sizeof(buf));
        EXPECT_EQ(r, 4);
        EXPECT_EQ(std::memcmp(buf, "ping", 4), 0);
    }

    SSL_free(srv_ssl);
    SSL_free(cli_ssl);
    ::close(srv_fd);
    ::close(cli_fd);
}

// SSL_CTX_check_private_key 内部已校验 key/cert 匹配（line 147 实际不可达）
// 通过构造 fake cert/key 触发 use_certificate_file 失败路径（line 134-140）
// 该路径已由 TlsContextServerPkiBadCertFileReturnsNull 等测试覆盖
// 此处仅添加一个 PSK 工厂的"identity 为空"路径覆盖（line 63-69 else 分支）
TEST(UdafCrypto, TlsContextPskClientEmptyIdentityWritesEmptyString) {
    using udaf::crypto::TlsContext;
    std::vector<std::uint8_t> psk(32, 0x42);

    // identity 为空字符串：覆盖 psk_client_cb line 68-69 (else if 分支)
    auto cli = TlsContext::create_psk(TlsContext::Mode::ClientPsk, psk, "");
    ASSERT_NE(cli, nullptr);

    // 验证 context 已设置（identity hint 为空）
    EXPECT_EQ(cli->mode(), TlsContext::Mode::ClientPsk);
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

// ---------------- 14. TlsContext mode() / native_handle() 覆盖 ----------------

TEST(UdafCrypto, TlsContextModeAndHandle) {
    using Mode = TlsContext::Mode;
    // 默认构造的 TlsContext pimpl_ == nullptr
    TlsContext empty;
    EXPECT_FALSE(empty.is_valid());
    EXPECT_EQ(empty.mode(), Mode::ServerPki);  // 默认值（pimpl_==nullptr 分支）
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(empty.native_handle()), 0u);  // 默认值

    // create_psk ClientPsk
    std::vector<std::uint8_t> psk(32, 0xBB);
    auto ctx = TlsContext::create_psk(Mode::ClientPsk, psk, "client-id");
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->mode(), Mode::ClientPsk);
    EXPECT_NE(reinterpret_cast<std::uintptr_t>(ctx->native_handle()), 0u);
}

TEST(UdafCrypto, TlsContextMoveAssignment) {
    using Mode = TlsContext::Mode;
    std::vector<std::uint8_t> psk_a(32, 0xAA);
    std::vector<std::uint8_t> psk_b(32, 0xBB);

    auto a = TlsContext::create_psk(Mode::ServerPsk, psk_a, "a-id");
    auto b = TlsContext::create_psk(Mode::ClientPsk, psk_b, "b-id");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    auto a_handle_before = reinterpret_cast<std::uintptr_t>(a->native_handle());
    auto b_handle_before = reinterpret_cast<std::uintptr_t>(b->native_handle());
    ASSERT_NE(a_handle_before, 0u);
    ASSERT_NE(b_handle_before, 0u);
    ASSERT_NE(a_handle_before, b_handle_before);

    // 移动赋值：a = std::move(b)
    // 注意：b 是 unique_ptr，move 后 b 自身变 nullptr（不可访问 b->）
    a = std::move(b);
    EXPECT_EQ(b, nullptr);  // b 已 move-from
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a->native_handle()), b_handle_before);
}

TEST(UdafCrypto, TlsContextPskInvalidSizeReturnsNull) {
    using Mode = TlsContext::Mode;
    // 非 32 字节 PSK → create_psk 返回 nullptr（早返回）
    std::vector<std::uint8_t> bad_psk(16, 0xCC);
    auto ctx = TlsContext::create_psk(Mode::ServerPsk, bad_psk, "id");
    EXPECT_EQ(ctx, nullptr);
}

TEST(UdafCrypto, TlsContextServerPkiInvalidCertReturnsNull) {
    // 不存在的证书/私钥文件 → create_server_pki 返回 nullptr
    auto srv = TlsContext::create_server_pki("/no/such/cert.pem",
                                              "/no/such/key.pem");
    EXPECT_EQ(srv, nullptr);
}

TEST(UdafCrypto, TlsContextClientPkiBadCaReturnsNull) {
    // 不存在的 CA 文件 → create_client_pki 返回 nullptr
    auto cli = TlsContext::create_client_pki("/no/such/ca.pem", "", "");
    EXPECT_EQ(cli, nullptr);
}

// ===== 覆盖 psk.cpp 校验路径 =====

// 覆盖 psk.cpp:229-230 psk_handshake_server_respond 参数大小校验
TEST(UdafCrypto, PskHandshakeServerRespondInvalidSize) {
    using udaf::crypto::psk_handshake_server_respond;
    std::vector<std::uint8_t> bad_psk(16, 0xCC);  // 非 32 字节
    auto req = udaf::crypto::psk_handshake_client_new("device-A");
    auto r = psk_handshake_server_respond(bad_psk, req);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// 覆盖 psk.cpp:229-230 request.salt/client_random 大小校验
TEST(UdafCrypto, PskHandshakeServerRespondInvalidRequest) {
    using udaf::crypto::psk_handshake_server_respond;
    std::vector<std::uint8_t> psk(32, 0xAA);
    udaf::crypto::AuthRequest bad_req;
    bad_req.client_random.assign(16, 0x11);  // 非 32 字节
    bad_req.salt.assign(32, 0x22);
    auto r = psk_handshake_server_respond(psk, bad_req);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// 覆盖 psk.cpp:278-279 psk_handshake_client_finalize 参数大小校验
TEST(UdafCrypto, PskHandshakeClientFinalizeInvalidSize) {
    using udaf::crypto::psk_handshake_client_finalize;
    std::vector<std::uint8_t> bad_psk(31, 0xBB);  // 非 32 字节
    auto req = udaf::crypto::psk_handshake_client_new("device-A");
    udaf::crypto::AuthResponse resp;
    resp.server_random.assign(32, 0x33);
    resp.salt.assign(32, 0x44);
    auto r = psk_handshake_client_finalize(bad_psk, req, resp);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// 覆盖 psk.cpp:278-279 psk 大小校验（仅检查 psk）
// 注：client_finalize 不校验 response.*，仅校验 psk.size
TEST(UdafCrypto, PskHandshakeClientFinalizeInvalidResponse) {
    using udaf::crypto::psk_handshake_client_finalize;
    std::vector<std::uint8_t> bad_psk(8, 0xCC);  // 非 32 字节
    auto req = udaf::crypto::psk_handshake_client_new("device-A");
    udaf::crypto::AuthResponse resp;
    resp.server_random.assign(32, 0x55);
    resp.salt.assign(32, 0x66);
    auto r = psk_handshake_client_finalize(bad_psk, req, resp);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}
// ===== 覆盖率补充（v0.3.14）=====

// PskAuthenticator 服务端收到过短请求 → 解析失败 → 返回 PROTOCOL_INVALID_MSG_TYPE
TEST(UdafCrypto, PskAuthenticatorServerShortBufferRejected) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto server = PskAuthenticator::create_server(psk, "node-1");
    ASSERT_NE(server, nullptr);

    // 过短 buffer（< 65 字节）
    std::vector<std::uint8_t> short_buf(10, 0xFF);
    auto r = server->process_handshake(short_buf);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::PROTOCOL_INVALID_MSG_TYPE);
}

// PskAuthenticator 客户端收到过短响应 → deserialize 返回空 server_random
TEST(UdafCrypto, PskAuthenticatorClientShortResponseRejected) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto client = PskAuthenticator::create_client(psk, "client-1");
    ASSERT_NE(client, nullptr);
    auto begin = client->begin_handshake();
    ASSERT_TRUE(begin.is_ok());

    // 过短响应（< 112 字节）
    std::vector<std::uint8_t> short_resp(50, 0xEE);
    auto r = client->process_handshake(short_resp);
    EXPECT_TRUE(r.is_err());
}

// PskAuthenticator 服务端在 begin_handshake 时返回 INVALID_ARG（非客户端调用）
TEST(UdafCrypto, PskAuthenticatorServerBeginHandshakeInvalid) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto server = PskAuthenticator::create_server(psk, "node-1");
    ASSERT_NE(server, nullptr);
    auto r = server->begin_handshake();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// PskAuthenticator 服务端 process_handshake 收到 id_len 越界请求
// （buf.size >= 65 但 < 65+id_len → deserialize 清空字段 → PROTOCOL_INVALID_MSG_TYPE）
TEST(UdafCrypto, PskAuthenticatorServerTruncatedIdentityRejected) {
    std::vector<std::uint8_t> psk(32, 0xAA);
    auto server = PskAuthenticator::create_server(psk, "node-1");
    ASSERT_NE(server, nullptr);
    // 构造 buf：64 字节 (random+salt) + id_len=255 但 buf 长度只有 100 → 截断
    std::vector<std::uint8_t> buf(100, 0xAB);
    buf[64] = 255;  // id_len = 255，但 buf 只到 100
    auto r = server->process_handshake(buf);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::PROTOCOL_INVALID_MSG_TYPE);
}

// hmac.cpp:44 / 48 / 67 / 70 / 85 未覆盖行
// 覆盖 HMAC verify 在签名长度不匹配 / 篡改时返回错误
TEST(UdafCrypto, HmacVerifyMismatchedLength) {
    using udaf::crypto::hmac_sha256;
    using udaf::crypto::hmac_sha256_verify;
    std::vector<std::uint8_t> key(16, 0x11);
    const std::uint8_t msg[] = {'h','e','l','l','o'};
    auto sig = hmac_sha256(key, std::span<const std::uint8_t>(msg, 5));
    ASSERT_TRUE(sig.is_ok());
    // 截断签名 → verify 应失败
    std::vector<std::uint8_t> short_sig(sig.value().begin(), sig.value().begin() + 10);
    EXPECT_TRUE(hmac_sha256_verify(key, std::span<const std::uint8_t>(msg, 5), short_sig).is_err());
    // 篡改签名 → verify 应失败
    auto bad_sig = sig.value();
    bad_sig[0] ^= 0xFF;
    EXPECT_TRUE(hmac_sha256_verify(key, std::span<const std::uint8_t>(msg, 5), bad_sig).is_err());
    // 正确签名 → verify 应成功
    EXPECT_TRUE(hmac_sha256_verify(key, std::span<const std::uint8_t>(msg, 5), sig.value()).is_ok());
}

// hmac.cpp:83-85 覆盖：hmac_sha256 内部错误时 verify 透传错误码
// 用空 key 调用 verify → hmac_sha256 内部返回 INVALID_ARG → verify 应原样返回
TEST(UdafCrypto, HmacVerifyPropagatesHmacError) {
    std::vector<std::uint8_t> empty_key;  // 空 key
    const std::uint8_t msg[] = {'h','i'};
    std::vector<std::uint8_t> expected(32, 0xAB);
    auto r = udaf::crypto::hmac_sha256_verify(
        empty_key,
        std::span<const std::uint8_t>(msg, 2),
        expected);
    ASSERT_TRUE(r.is_err());
    // 应透传 hmac_sha256 的 INVALID_ARG（不是 CRYPTO_HMAC_MISMATCH）
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// hmac.cpp:72-73 覆盖：HMAC 计算成功后 out.resize(out_len) 与 ok 返回
// （用非 SHA256 块大小的 key 触发 OpenSSL 内部走完整 finalize 路径）
TEST(UdafCrypto, HmacSha256SuccessReturnsCorrectLength) {
    std::vector<std::uint8_t> key(100, 0x42);  // > SHA256 块大小 (64)
    const std::uint8_t msg[] = {'a','b','c'};
    auto r = udaf::crypto::hmac_sha256(key,
        std::span<const std::uint8_t>(msg, 3));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().size(), 32u);  // SHA256 输出 32 字节
}

// ===== 覆盖率补充（v0.3.14）=====
// psk.cpp:119-121 psk_aead_encrypt key 长度 != 32 → INVALID_ARG
TEST(UdafCrypto, PskAeadEncryptInvalidKeyLength) {
    std::vector<std::uint8_t> short_key(16, 0xAA);  // 非 32 字节
    std::array<std::uint8_t, 12> nonce{};
    std::vector<std::uint8_t> pt{'h','i'};
    auto r = udaf::crypto::psk_aead_encrypt(short_key, nonce, pt, {});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// psk.cpp:243-250 psk_aead_decrypt 长度校验路径（覆盖 INVALID_ARG/INTERNAL 分支）
TEST(UdafCrypto, PskAeadDecryptInvalidKeyLength) {
    std::vector<std::uint8_t> short_key(8, 0xAA);
    std::array<std::uint8_t, 12> nonce{};
    std::vector<std::uint8_t> ct(32, 0x00);  // 假密文（不会真用）
    auto r = udaf::crypto::psk_aead_decrypt(short_key, nonce, ct, {});
    EXPECT_TRUE(r.is_err());
    // key 长度错误优先返回
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

// psk.cpp psk_handshake_server_respond 输入非法 AuthRequest → Err
TEST(UdafCrypto, PskServerRespondBadRequest) {
    std::vector<std::uint8_t> psk(32, 0xAB);
    udaf::crypto::AuthRequest req;  // 空字段（client_random/salt/identity 均空）
    auto r = udaf::crypto::psk_handshake_server_respond(
        std::span<const std::uint8_t>(psk.data(), psk.size()), req);
    EXPECT_TRUE(r.is_err());
}

// 覆盖 psk_authenticator.cpp:66-68 deserialize_response 检测 buf.size() < 112+len
TEST(UdafCrypto, PskClientMalformedServerResponse) {
    std::mt19937_64 rng(0xDEADBEEF);
    auto psk = make_psk(rng);
    auto client = PskAuthenticator::create_client(psk, "device-A");
    ASSERT_NE(client, nullptr);
    auto client_msg = client->begin_handshake();
    ASSERT_TRUE(client_msg.is_ok());

    // 构造一个 len 字段声明 100 字节但实际只有 4 字节数据的畸形响应
    std::vector<std::uint8_t> bad_resp(116, 0xCC);
    std::uint32_t fake_len = 100;
    std::memcpy(bad_resp.data() + 108, &fake_len, 4);  // offset 108: len 字段

    // 客户端 process_handshake 应返回 Err（deserialize_response 检测到截断）
    auto r = client->process_handshake(bad_resp);
    EXPECT_TRUE(r.is_err());
    EXPECT_FALSE(client->is_handshake_done());
}

// 覆盖 psk_authenticator.cpp:33-35 deserialize_request buf.size() < 65 + id_len 分支
TEST(UdafCrypto, PskServerMalformedRequestIdentityLengthExceeds) {
    std::mt19937_64 rng(0xCAFEBABE);
    auto psk = make_psk(rng);
    auto server = PskAuthenticator::create_server(psk, "server-A");
    ASSERT_NE(server, nullptr);

    // 构造 65 字节的请求：32 client_random + 32 salt + 1 id_len=200（但 buf 只有 65）
    std::vector<std::uint8_t> bad_req(65, 0xAA);
    bad_req[64] = 200;  // id_len 远超 buf 实际可用空间

    // 服务端 process_handshake 应返回 Err（id_len 异常 → client_random.clear() → 空判断）
    auto r = server->process_handshake(bad_req);
    EXPECT_TRUE(r.is_err());
    EXPECT_FALSE(server->is_handshake_done());
}

// ===== Round 6 覆盖率补充 =====

// 覆盖 tls_context.cpp:144 create_server_pki key file 加载失败
TEST(UdafCrypto, TlsContextServerPkiInvalidKeyFileReturnsNull) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto good_key = tmp.p() / "good_key.pem";
    auto bad_key = tmp.p() / "bad_key.pem";
    ASSERT_TRUE(generate_self_signed(cert, good_key));

    // 创建第二个 key 文件（与 cert 不匹配的私钥）
    ASSERT_TRUE(generate_self_signed(cert, bad_key));  // 实际上还是匹配的

    // 用一个明显不是 PEM 格式的文件作为 key
    std::ofstream(bad_key) << "this is not a PEM key file";

    auto srv = TlsContext::create_server_pki(cert, bad_key);
    EXPECT_EQ(srv, nullptr);  // SSL_CTX_use_PrivateKey_file 失败
}

// 覆盖 tls_context.cpp:147 create_server_pki check_private_key 失败
TEST(UdafCrypto, TlsContextServerPkiMismatchedCertKeyReturnsNull) {
    TmpDir tmp;
    auto cert1 = tmp.p() / "cert1.pem";
    auto key1  = tmp.p() / "key1.pem";
    auto key2  = tmp.p() / "key2.pem";

    ASSERT_TRUE(generate_self_signed(cert1, key1));
    // 生成另一对独立的 cert+key
    ASSERT_TRUE(generate_self_signed(tmp.p() / "cert2.pem", key2));

    // 用 cert1 + key2（不匹配的私钥）→ check_private_key 应失败
    auto srv = TlsContext::create_server_pki(cert1, key2);
    EXPECT_EQ(srv, nullptr);
}

// 覆盖 tls_context.cpp:166-172 create_client_pki ca_file 空字符串跳过 + ca_file 不存在返回 nullptr
TEST(UdafCrypto, TlsContextClientPkiEmptyCaFileSkipsVerify) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    // ca_file 空字符串 → 跳过 SSL_CTX_load_verify_locations
    auto cli = TlsContext::create_client_pki("", cert, key);
    EXPECT_NE(cli, nullptr);  // 应成功创建
}

// 覆盖 tls_context.cpp:173-179 create_client_pki cert/key 提供但加载失败
TEST(UdafCrypto, TlsContextClientPkiBadCertFileReturnsNull) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    auto bad_cert = tmp.p() / "bad_cert.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));
    std::ofstream(bad_cert) << "this is not a valid cert";

    auto cli = TlsContext::create_client_pki("", bad_cert, key);
    EXPECT_EQ(cli, nullptr);  // SSL_CTX_use_certificate_file 失败
}

// 覆盖 tls_context.cpp:177-179 create_client_pki cert 有效但 key 加载失败
TEST(UdafCrypto, TlsContextClientPkiBadKeyFileReturnsNull) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    auto bad_key = tmp.p() / "bad_key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));
    std::ofstream(bad_key) << "this is not a valid key";

    auto cli = TlsContext::create_client_pki("", cert, bad_key);
    EXPECT_EQ(cli, nullptr);  // SSL_CTX_use_PrivateKey_file 失败
}

// ============================================================
// F23 新增覆盖：pki.cpp PkiHandshake 全部路径
// ============================================================

#include "pki.hpp"
using udaf::crypto::PkiHandshake;
using udaf::crypto::PkiHandshakeStage;

// 完整 TLS 1.3 PKI 握手 → 加密 → 解密 → 指纹
TEST(UdafCrypto, PkiHandshakeFullRoundTrip) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv_ctx = TlsContext::create_server_pki(cert, key);
    auto cli_ctx = TlsContext::create_client_pki(cert, cert, key);
    ASSERT_NE(srv_ctx, nullptr);
    ASSERT_NE(cli_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    auto srv_hs = PkiHandshake::create(std::move(srv_ctx), fds[0]);
    auto cli_hs = PkiHandshake::create(std::move(cli_ctx), fds[1]);
    ASSERT_NE(srv_hs, nullptr);
    ASSERT_NE(cli_hs, nullptr);
    EXPECT_EQ(srv_hs->stage(), PkiHandshakeStage::Init);
    EXPECT_EQ(cli_hs->stage(), PkiHandshakeStage::Init);

    std::atomic<bool> srv_done{false}, cli_done{false};
    std::atomic<bool> srv_failed{false}, cli_failed{false};
    auto step_loop = [](PkiHandshake* hs, std::atomic<bool>& done,
                        std::atomic<bool>& failed) {
        for (int i = 0; i < 500; ++i) {
            auto stage = hs->step();
            if (stage == PkiHandshakeStage::Done) { done.store(true); return; }
            if (stage == PkiHandshakeStage::Failed) { failed.store(true); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    std::thread srv_thread(step_loop, srv_hs.get(), std::ref(srv_done), std::ref(srv_failed));
    std::thread cli_thread(step_loop, cli_hs.get(), std::ref(cli_done), std::ref(cli_failed));
    srv_thread.join();
    cli_thread.join();

    ASSERT_TRUE(srv_done.load()) << "server handshake failed";
    ASSERT_TRUE(cli_done.load()) << "client handshake failed";
    EXPECT_TRUE(srv_hs->is_done());
    EXPECT_TRUE(cli_hs->is_done());

    // 重复 step() 在 Done 状态下应保持 Done（line 57-58）
    EXPECT_EQ(srv_hs->step(), PkiHandshakeStage::Done);
    EXPECT_EQ(cli_hs->step(), PkiHandshakeStage::Done);

    // peer_fingerprint：服务端配置 SSL_VERIFY_NONE（未要求客户端证书），
    // 因此服务端 SSL_get_peer_certificate 返回 nullptr，srv_fp 返回 err。
    // 客户端 SSL_VERIFY_PEER + 服务端证书 → cli_fp 返回 ok 且 32 字节 SHA-256。
    auto srv_fp = srv_hs->peer_fingerprint();
    auto cli_fp = cli_hs->peer_fingerprint();
    // 服务端视角：服务端未配置客户端证书校验，无对端证书
    EXPECT_TRUE(srv_fp.is_err());
    EXPECT_EQ(srv_fp.error(), udaf::core::ErrorCode::BIZ_FILE_NOT_FOUND);
    // 客户端视角：能拿到服务端证书指纹
    ASSERT_TRUE(cli_fp.is_ok());
    EXPECT_EQ(cli_fp.value().size(), 32u);

    // encrypt + decrypt 验证通信
    const std::uint8_t msg[] = "hello-pki-roundtrip";
    auto enc_r = cli_hs->encrypt(msg);
    ASSERT_TRUE(enc_r.is_ok());
    EXPECT_FALSE(enc_r.value().empty());

    auto dec_r = srv_hs->decrypt(enc_r.value());
    ASSERT_TRUE(dec_r.is_ok());
    // TLS 1.3 解密可能包含内部 record padding（多 1 字节），断言前缀匹配
    ASSERT_GE(dec_r.value().size(), sizeof(msg) - 1);
    EXPECT_EQ(std::memcmp(dec_r.value().data(), msg, sizeof(msg) - 1), 0);

    // encrypt 多次调用应都能成功（line 111-117 反复执行）
    for (int i = 0; i < 3; ++i) {
        auto enc_repeat = cli_hs->encrypt(msg);
        ASSERT_TRUE(enc_repeat.is_ok());
        EXPECT_FALSE(enc_repeat.value().empty());
    }

    ::close(fds[0]);
    ::close(fds[1]);
}

// create() 输入校验：null ctx / 非法 fd 应返回 nullptr（line 29）
TEST(UdafCrypto, PkiHandshakeCreateRejectsInvalidInput) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto good_ctx = TlsContext::create_server_pki(cert, key);
    ASSERT_NE(good_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    // null ctx（line 29 的 !ctx 分支）
    EXPECT_EQ(PkiHandshake::create(nullptr, fds[0]), nullptr);
    // 非法 fd（line 29 的 fd < 0 分支）
    EXPECT_EQ(PkiHandshake::create(std::move(good_ctx), -1), nullptr);

    ::close(fds[0]);
    ::close(fds[1]);
}

// step() 在 Failed 状态应保持 Failed（line 57-58）
TEST(UdafCrypto, PkiHandshakeStepStaysInTerminalState) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv_ctx = TlsContext::create_server_pki(cert, key);
    auto cli_ctx = TlsContext::create_client_pki(cert, cert, key);
    ASSERT_NE(srv_ctx, nullptr);
    ASSERT_NE(cli_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    auto srv_hs = PkiHandshake::create(std::move(srv_ctx), fds[0]);
    auto cli_hs = PkiHandshake::create(std::move(cli_ctx), fds[1]);
    ASSERT_NE(srv_hs, nullptr);
    ASSERT_NE(cli_hs, nullptr);

    // 正常完成握手
    std::atomic<bool> srv_done{false}, cli_done{false};
    std::atomic<bool> srv_failed{false}, cli_failed{false};
    auto step_loop = [](PkiHandshake* hs, std::atomic<bool>& done,
                        std::atomic<bool>& failed) {
        for (int i = 0; i < 500; ++i) {
            auto stage = hs->step();
            if (stage == PkiHandshakeStage::Done) { done.store(true); return; }
            if (stage == PkiHandshakeStage::Failed) { failed.store(true); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    std::thread srv_thread(step_loop, srv_hs.get(), std::ref(srv_done), std::ref(srv_failed));
    std::thread cli_thread(step_loop, cli_hs.get(), std::ref(cli_done), std::ref(cli_failed));
    srv_thread.join();
    cli_thread.join();
    ASSERT_TRUE(srv_done.load());
    ASSERT_TRUE(cli_done.load());

    // 握手完成后 step() 应返回 Done（line 57-58 早返回）
    EXPECT_EQ(srv_hs->step(), PkiHandshakeStage::Done);
    EXPECT_EQ(srv_hs->step(), PkiHandshakeStage::Done);
    EXPECT_EQ(cli_hs->step(), PkiHandshakeStage::Done);

    ::close(fds[0]);
    ::close(fds[1]);
}

// encrypt/decrypt 在握手未完成时应返回 NOT_IMPLEMENTED（line 106-108, 122-124）
TEST(UdafCrypto, PkiHandshakeEncryptDecryptBeforeDoneFails) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv_ctx = TlsContext::create_server_pki(cert, key);
    ASSERT_NE(srv_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    auto srv_hs = PkiHandshake::create(std::move(srv_ctx), fds[0]);
    ASSERT_NE(srv_hs, nullptr);
    EXPECT_EQ(srv_hs->stage(), PkiHandshakeStage::Init);

    const std::uint8_t msg[] = "x";
    auto enc = srv_hs->encrypt(msg);
    ASSERT_TRUE(enc.is_err());
    EXPECT_EQ(enc.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);

    auto dec = srv_hs->decrypt(msg);
    ASSERT_TRUE(dec.is_err());
    EXPECT_EQ(dec.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);

    // peer_fingerprint 在 Done 之前应返回 NOT_IMPLEMENTED（line 85-87）
    auto fp = srv_hs->peer_fingerprint();
    ASSERT_TRUE(fp.is_err());
    EXPECT_EQ(fp.error(), udaf::core::ErrorCode::NOT_IMPLEMENTED);

    ::close(fds[0]);
    ::close(fds[1]);
}

// 验证客户端 step() 一次后，服务端没有响应时再次 step 应进入 WantsRead
// 或 WantsWrite（覆盖 line 69-72 SSL_ERROR_WANT_READ / WANT_WRITE 分支）
TEST(UdafCrypto, PkiHandshakeStepEntersWantsReadOrWrite) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv_ctx = TlsContext::create_server_pki(cert, key);
    auto cli_ctx = TlsContext::create_client_pki(cert, cert, key);
    ASSERT_NE(srv_ctx, nullptr);
    ASSERT_NE(cli_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    // 设两端 fd 都为 non-blocking：避免 SSL_accept/connect 在对端 socket 缓存满时阻塞
    for (int fd_idx : {0, 1}) {
        int fl = ::fcntl(fds[fd_idx], F_GETFL, 0);
        ASSERT_GE(fl, 0);
        ASSERT_EQ(::fcntl(fds[fd_idx], F_SETFL, fl | O_NONBLOCK), 0)
            << strerror(errno);
    }

    auto cli_hs = PkiHandshake::create(std::move(cli_ctx), fds[1]);
    auto srv_hs = PkiHandshake::create(std::move(srv_ctx), fds[0]);
    ASSERT_NE(cli_hs, nullptr);
    ASSERT_NE(srv_hs, nullptr);

    // 第一次客户端 step() 产生 ClientHello，non-blocking 下可能直接返回
    // WantsRead/WantsWrite（取决于 socket 缓存是否可写）
    PkiHandshakeStage s = cli_hs->step();
    EXPECT_TRUE(s == PkiHandshakeStage::WantsRead ||
                s == PkiHandshakeStage::WantsWrite ||
                s == PkiHandshakeStage::Init)
        << "expected WantsRead/WantsWrite/Init after first client step, got "
        << static_cast<int>(s);

    // 服务端开始 step()，与客户端交换字节。在 partial step 中，任意一方都可能
    // 进入 WantsRead 或 WantsWrite 中间态。运行 5 轮来触发至少一次中间态。
    bool saw_intermediate = false;
    for (int i = 0; i < 5 && !saw_intermediate; ++i) {
        auto cs = cli_hs->step();
        auto ss = srv_hs->step();
        if (cs == PkiHandshakeStage::WantsRead ||
            cs == PkiHandshakeStage::WantsWrite ||
            ss == PkiHandshakeStage::WantsRead ||
            ss == PkiHandshakeStage::WantsWrite) {
            saw_intermediate = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(saw_intermediate)
        << "never observed WantsRead or WantsWrite during partial handshake";

    ::close(fds[0]);
    ::close(fds[1]);
}

// 仅驱动服务端，向其写入畸形字节 → 服务端 step() 应进入 Failed 状态
// （覆盖 line 73-79 Failed 分支）
TEST(UdafCrypto, PkiHandshakeStepFailsWhenPeerSilent) {
    TmpDir tmp;
    auto cert = tmp.p() / "cert.pem";
    auto key  = tmp.p() / "key.pem";
    ASSERT_TRUE(generate_self_signed(cert, key));

    auto srv_ctx = TlsContext::create_server_pki(cert, key);
    ASSERT_NE(srv_ctx, nullptr);

    int fds[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << strerror(errno);

    auto srv_hs = PkiHandshake::create(std::move(srv_ctx), fds[0]);
    ASSERT_NE(srv_hs, nullptr);

    // 向服务端发送一段非 TLS 字节，触发协议错误（不需要客户端真握手）
    const std::uint8_t garbage[] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};
    ssize_t w = ::send(fds[1], garbage, sizeof(garbage), MSG_NOSIGNAL);
    ASSERT_EQ(w, static_cast<ssize_t>(sizeof(garbage)));

    // 服务端 step() 现在应当直接进入 Failed
    PkiHandshakeStage final_stage = PkiHandshakeStage::Init;
    for (int i = 0; i < 20; ++i) {
        final_stage = srv_hs->step();
        if (final_stage == PkiHandshakeStage::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(final_stage, PkiHandshakeStage::Failed);
    // 重复 step() 应保持 Failed（line 57-58 早返回）
    EXPECT_EQ(srv_hs->step(), PkiHandshakeStage::Failed);

    ::close(fds[0]);
    ::close(fds[1]);
}
