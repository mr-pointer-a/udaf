// fuzz_main.cpp - Harness-style 模糊测试入口
//
// 用法：./udaf_fuzz <iterations>
// 在 ASan + UBSan 构建下运行；随机生成畸形输入验证解析器不崩溃
//
// 覆盖：
//   - ability_a::discovery::Advertisement::parse_payload (magic+nonce+hmac)
//   - crypto::psk::psk_handshake_client_finalize (畸形 response)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <vector>

#include "ability_a/discovery/advertisement.hpp"
#include "crypto/psk.hpp"
#include "crypto/auth_types.hpp"

namespace {

std::vector<std::uint8_t> gen_random(std::size_t n, std::mt19937& rng) {
    std::uniform_int_distribution<int> d(0, 255);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(d(rng));
    return v;
}

template <typename Fn>
bool fuzz_loop(const char* name, std::size_t iterations, Fn fn) {
    std::random_device rd;
    std::mt19937 rng(rd());
    int crashes = 0;
    for (std::size_t i = 0; i < iterations; ++i) {
        std::size_t n = 0;
        // 50% 极短输入，50% 中等输入
        if (i % 2 == 0) {
            n = i % 32;  // 0..31
        } else {
            n = 32 + (i % 1024);
        }
        auto buf = gen_random(n, rng);
        try {
            fn(buf);
        } catch (...) {
            std::fprintf(stderr, "[%s] iter=%zu UNEXPECTED EXCEPTION\n", name, i);
            ++crashes;
        }
    }
    std::printf("[fuzz] %s: iterations=%zu crashes=%d\n", name, iterations, crashes);
    return crashes == 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t iterations = 10000;
    if (argc > 1) iterations = std::strtoull(argv[1], nullptr, 10);

    bool ok = true;

    // Fuzz #1: Advertisement::parse_payload
    ok &= fuzz_loop("Advertisement::parse_payload", iterations,
        [](const std::vector<std::uint8_t>& buf) {
            std::span<const std::uint8_t> s(buf.data(), buf.size());
            (void)udaf::ability_a::discovery::parse_payload(s);
        });

    // Fuzz #2: PSK client finalize (畸形 AuthRequest/AuthResponse)
    ok &= fuzz_loop("PSK finalize", iterations,
        [](const std::vector<std::uint8_t>& buf) {
            if (buf.size() < 4) return;
            std::span<const std::uint8_t> salt(buf.data(), std::min<std::size_t>(buf.size(), 16));
            std::vector<std::uint8_t> psk(32);
            std::memcpy(psk.data(), buf.data(), std::min(buf.size(), psk.size()));
            udaf::crypto::AuthRequest req;
            std::string id = "fuzz";
            req.identity.assign(id.begin(), id.end());
            req.client_random.resize(32);
            std::memcpy(req.client_random.data(), buf.data(), std::min(buf.size(), std::size_t{32}));
            req.salt.assign(salt.begin(), salt.end());
            udaf::crypto::AuthResponse resp;
            resp.server_random.resize(32);
            resp.salt.assign(salt.begin(), salt.end());
            if (buf.size() > 32) {
                resp.encrypted_session_key.assign(buf.begin() + 32,
                    buf.begin() + std::min<std::size_t>(buf.size(), 32 + 48));
            }
            // resp.nonce/mac 是 std::array，不能 resize
            std::memset(resp.nonce.data(), 0, resp.nonce.size());
            std::memset(resp.mac.data(), 0, resp.mac.size());
            (void)udaf::crypto::psk_handshake_client_finalize(psk, req, resp);
        });

    return ok ? 0 : 1;
}