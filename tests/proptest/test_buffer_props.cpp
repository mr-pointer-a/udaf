// test_buffer_props.cpp - RingBuffer / DynamicRingBuffer 属性测试
//
// 不变量：
//   - RingBuffer：write N 字节后 read N 字节得到原序列（FIFO）
//   - DynamicRingBuffer：跨边界写入读取正确（环绕）
//   - 容量边界：超过 max_entry 写入返回 PROTOCOL_PAYLOAD_TOO_LARGE
//   - 数据完整性：write 后立即 read 不丢失数据

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "core/buffer/buffer.hpp"
#include "core/error_code.hpp"

using udaf::core::RingBuffer;
using udaf::core::DynamicRingBuffer;
using udaf::core::ErrorCode;

namespace {

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& v) {
    std::vector<std::byte> out;
    out.reserve(v.size());
    for (auto b : v) out.push_back(static_cast<std::byte>(b));
    return out;
}

std::vector<std::uint8_t> to_uint8(std::span<const std::byte> s) {
    std::vector<std::uint8_t> out;
    out.reserve(s.size());
    for (auto b : s) out.push_back(static_cast<std::uint8_t>(b));
    return out;
}

// 把外部的循环限制次数参数化（不超过 50 字节）。
std::size_t clamp_count(std::size_t n) noexcept {
    return std::min(n, static_cast<std::size_t>(50));
}

}  // namespace

RC_GTEST_PROP(BufferProps, RingBufferWriteReadRoundTrip, (const std::vector<std::uint8_t>& data)) {
    // RingBuffer<256>：max_entry = 256 - 4 = 252，把数据裁剪到 200 字节以内。
    if (data.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    constexpr std::size_t kMaxPayload = 200;
    const std::size_t cap = std::min(data.size(), kMaxPayload);
    std::vector<std::uint8_t> trimmed(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cap));

    RingBuffer<256> rb;
    auto bytes = to_bytes(trimmed);
    auto w = rb.write(std::span<const std::byte>(bytes.data(), bytes.size()));
    RC_ASSERT(w.is_ok());

    std::vector<std::byte> out(bytes.size());
    auto r = rb.read(std::span<std::byte>(out.data(), out.size()));
    RC_ASSERT(r.is_ok());
    RC_ASSERT(r.value() == bytes.size());
    RC_ASSERT(to_uint8(std::span<const std::byte>(out.data(), out.size())) == trimmed);
}

RC_GTEST_PROP(BufferProps, DynamicRingBufferWriteReadRoundTrip,
              (const std::vector<std::uint8_t>& data)) {
    // DynamicRingBuffer(1024)：max_entry = 1024 - 4 = 1020，足够装下 RC 默认生成的随机数据。
    if (data.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    constexpr std::size_t kMaxPayload = 500;
    const std::size_t cap = std::min(data.size(), kMaxPayload);
    std::vector<std::uint8_t> trimmed(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cap));

    DynamicRingBuffer rb(1024);
    auto bytes = to_bytes(trimmed);
    auto w = rb.write(std::span<const std::byte>(bytes.data(), bytes.size()));
    RC_ASSERT(w.is_ok());

    std::vector<std::byte> out(bytes.size());
    auto r = rb.read(std::span<std::byte>(out.data(), out.size()));
    RC_ASSERT(r.is_ok());
    RC_ASSERT(r.value() == bytes.size());
    RC_ASSERT(to_uint8(std::span<const std::byte>(out.data(), out.size())) == trimmed);
}

RC_GTEST_PROP(BufferProps, DynamicRingBufferCrossBoundary,
              (const std::vector<std::uint8_t>& data)) {
    // 容量 8（小缓冲），max_entry = 4 字节。反复 write/read 单字节，强制环绕。
    if (data.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    DynamicRingBuffer rb(8);

    std::size_t written = 0;
    std::vector<std::uint8_t> reconstructed;
    reconstructed.reserve(data.size());

    const std::size_t limit = clamp_count(data.size());
    for (std::size_t i = 0; i < limit; ++i) {
        std::array<std::byte, 1> one{{static_cast<std::byte>(data[written])}};
        auto w = rb.write(std::span<const std::byte>(one.data(), 1));
        if (w.is_ok()) {
            ++written;
            // 每写 3 字节就读 2 字节，强制环绕
            if (written % 3 == 0) {
                std::array<std::byte, 2> read_buf{};
                auto r = rb.read(std::span<std::byte>(read_buf.data(), 2));
                if (r.is_ok() && r.value() == 2) {
                    reconstructed.push_back(static_cast<std::uint8_t>(read_buf[0]));
                    reconstructed.push_back(static_cast<std::uint8_t>(read_buf[1]));
                }
            }
        }
    }
    // 排空剩余
    std::array<std::byte, 8> tail_buf{};
    while (true) {
        auto r = rb.read(std::span<std::byte>(tail_buf.data(), tail_buf.size()));
        if (!r.is_ok() || r.value() == 0) break;
        for (std::size_t i = 0; i < r.value(); ++i) {
            reconstructed.push_back(static_cast<std::uint8_t>(tail_buf[i]));
        }
    }
    RC_ASSERT(reconstructed.size() == written);
    // 所有被成功 write 的字节必须按 FIFO 序被 read 出来
    for (std::size_t i = 0; i < written; ++i) {
        RC_ASSERT(reconstructed[i] == data[i]);
    }
}

RC_GTEST_PROP(BufferProps, DynamicRingBufferTooLargeReturnsTooLarge,
              (const std::vector<std::uint8_t>& data)) {
    // 容量 8 → max_entry = 4。data 大于 4 字节时必须返回 PROTOCOL_PAYLOAD_TOO_LARGE。
    DynamicRingBuffer rb(8);
    if (data.size() <= 4) {
        RC_SUCCEED("Skip within max_entry");
        return;
    }
    auto bytes = to_bytes(data);
    auto w = rb.write(std::span<const std::byte>(bytes.data(), bytes.size()));
    RC_ASSERT(w.is_err());
    RC_ASSERT(w.error() == ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
}

RC_GTEST_PROP(BufferProps, DynamicRingBufferEmptyReadReturnsExhausted,
              (const std::vector<std::uint8_t>& data)) {
    // 空缓冲的 read 必须返回 RES_MEMORY_EXHAUSTED。
    DynamicRingBuffer rb(64);
    if (data.empty()) {
        RC_SUCCEED("Skip empty");
        return;
    }
    std::array<std::byte, 16> buf{};
    auto r = rb.read(std::span<std::byte>(buf.data(), buf.size()));
    RC_ASSERT(r.is_err());
    RC_ASSERT(r.error() == ErrorCode::RES_MEMORY_EXHAUSTED);
}
