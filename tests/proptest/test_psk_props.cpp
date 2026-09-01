// test_psk_props.cpp - PSK AEAD 加解密属性测试
//
// 不变量：
//   - AEAD 解密 == 加密原文（除长度=0）
//   - 错误 PSK 解密必失败
//   - 错误 nonce 解密必失败（AEAD 完整性）
//   - 篡改密文 1 字节必失败（AEAD integrity）

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "crypto/psk.hpp"

using udaf::crypto::psk_aead_encrypt;
using udaf::crypto::psk_aead_decrypt;
using udaf::crypto::Nonce;

RC_GTEST_PROP(PskProps, EncryptDecryptRoundTrip, (const std::vector<std::uint8_t>& plaintext)) {
    std::vector<std::uint8_t> psk(32, 0xA5);
    Nonce nonce{};
    for (std::size_t i = 0; i < 12 && i < plaintext.size(); ++i) {
        nonce[i] = static_cast<std::uint8_t>(plaintext[i] ^ 0x33);
    }

    auto enc = psk_aead_encrypt(psk, nonce, plaintext, {});
    RC_ASSERT(enc.is_ok());
    auto dec = psk_aead_decrypt(psk, nonce, enc.value(), {});
    RC_ASSERT(dec.is_ok());
    RC_ASSERT(dec.value() == plaintext);
}

RC_GTEST_PROP(PskProps, WrongPskFailsDecrypt, (const std::vector<std::uint8_t>& plaintext,
                                               std::uint8_t psk_delta)) {
    if (plaintext.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    std::vector<std::uint8_t> psk_a(32, 0xA5);
    std::vector<std::uint8_t> psk_b(32, 0xA5);
    psk_b[0] ^= static_cast<std::uint8_t>(psk_delta | 0x01);  // 保证不同

    Nonce nonce{};
    nonce[0] = 0x01;
    nonce[1] = 0x02;

    auto enc = psk_aead_encrypt(psk_a, nonce, plaintext, {});
    RC_ASSERT(enc.is_ok());

    auto dec = psk_aead_decrypt(psk_b, nonce, enc.value(), {});
    RC_ASSERT(dec.is_err());  // 错误 PSK 必须解密失败
}

RC_GTEST_PROP(PskProps, WrongNonceFailsDecrypt, (const std::vector<std::uint8_t>& plaintext,
                                                 std::uint8_t nonce_byte)) {
    if (plaintext.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    std::vector<std::uint8_t> psk(32, 0xC3);
    Nonce n1{};
    n1[0] = 0x00;
    Nonce n2 = n1;
    n2[0] = static_cast<std::uint8_t>(nonce_byte | 0x01);  // 保证与 n1 不同

    auto enc = psk_aead_encrypt(psk, n1, plaintext, {});
    RC_ASSERT(enc.is_ok());

    auto dec = psk_aead_decrypt(psk, n2, enc.value(), {});
    RC_ASSERT(dec.is_err());  // 错误 nonce 必须解密失败
}

RC_GTEST_PROP(PskProps, TamperedCiphertextFailsDecrypt, (const std::vector<std::uint8_t>& plaintext,
                                                       std::uint8_t tamper_index,
                                                       std::uint8_t tamper_byte)) {
    if (plaintext.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    std::vector<std::uint8_t> psk(32, 0x5A);
    Nonce nonce{};
    nonce[0] = 0x10;

    auto enc = psk_aead_encrypt(psk, nonce, plaintext, {});
    RC_ASSERT(enc.is_ok());

    auto ciphertext = enc.value();
    if (!ciphertext.empty()) {
        std::size_t idx = tamper_index % ciphertext.size();
        ciphertext[idx] ^= static_cast<std::uint8_t>(tamper_byte | 0x01);  // 保证改变

        auto dec = psk_aead_decrypt(psk, nonce, ciphertext, {});
        RC_ASSERT(dec.is_err());  // 篡改必须检测到
    }
}

RC_GTEST_PROP(PskProps, AadMustMatchForDecrypt, (const std::vector<std::uint8_t>& plaintext,
                                                const std::vector<std::uint8_t>& aad)) {
    if (plaintext.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    std::vector<std::uint8_t> psk(32, 0x77);
    Nonce nonce{};
    nonce[0] = 0xAB;

    auto enc = psk_aead_encrypt(psk, nonce, plaintext, aad);
    RC_ASSERT(enc.is_ok());

    // 使用不同 AAD 解密
    std::vector<std::uint8_t> wrong_aad = aad;
    if (!wrong_aad.empty()) {
        wrong_aad[0] ^= 0x01;
    } else {
        wrong_aad.push_back(0x42);
    }
    auto dec = psk_aead_decrypt(psk, nonce, enc.value(), wrong_aad);
    RC_ASSERT(dec.is_err());  // 错误 AAD 必须解密失败
}
