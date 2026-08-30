// test_buffer.cpp - RingBuffer / DynamicRingBuffer 测试（共 8 用例）
#include "buffer/buffer.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using udaf::core::RingBuffer;
using udaf::core::DynamicRingBuffer;
using udaf::core::ErrorCode;

namespace {

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.emplace_back(static_cast<std::byte>(c));
    return v;
}

}  // namespace

TEST(RingBuffer, WriteReadRoundTrip) {
    RingBuffer<1024> rb;
    auto data = bytes_of("hello udaf");
    ASSERT_TRUE(rb.write(data).is_ok());

    std::array<std::byte, 64> out{};
    auto r = rb.read(out);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), data.size());
    EXPECT_EQ(std::memcmp(out.data(), data.data(), data.size()), 0);
}

TEST(RingBuffer, ReadEmptyReturnsErr) {
    RingBuffer<1024> rb;
    std::array<std::byte, 64> out{};
    auto r = rb.read(out);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::RES_MEMORY_EXHAUSTED);
}

TEST(RingBuffer, WriteTooLargeReturnsErr) {
    RingBuffer<128> rb;
    std::vector<std::byte> big(RingBuffer<128>::kMaxEntryBytes + 1, std::byte{0});
    auto r = rb.write(big);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
}

TEST(RingBuffer, ReadIntoTooSmallBufferReturnsErr) {
    RingBuffer<1024> rb;
    auto data = bytes_of(std::string(50, 'x'));
    ASSERT_TRUE(rb.write(data).is_ok());

    std::array<std::byte, 32> out{};
    auto r = rb.read(out);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
}

TEST(RingBuffer, CrossBoundaryWrite) {
    // 写入多块小记录触发 head 跨越 capacity 边界
    RingBuffer<256> rb;
    // 先填一些数据再消费，强制 head 位于非零位置
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(rb.write(bytes_of("a")).is_ok());
        std::array<std::byte, 8> out{};
        ASSERT_TRUE(rb.read(out).is_ok());
    }
    // 再写一条较长记录，验证跨边界写入正确
    std::string payload(100, 'b');
    ASSERT_TRUE(rb.write(bytes_of(payload)).is_ok());

    std::vector<std::byte> out(120);
    auto r = rb.read(out);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 100u);
    EXPECT_EQ(std::memcmp(out.data(), payload.data(), 100), 0);
}

TEST(RingBuffer, PeekSizeWithoutConsuming) {
    RingBuffer<1024> rb;
    auto data = bytes_of("peek me");
    ASSERT_TRUE(rb.write(data).is_ok());

    auto peek = rb.peek_size();
    ASSERT_TRUE(peek.is_ok());
    EXPECT_EQ(peek.value(), data.size());

    // size 不变
    EXPECT_FALSE(rb.empty());

    // 正常读取
    std::array<std::byte, 32> out{};
    auto r = rb.read(out);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), data.size());
}

TEST(RingBuffer, SPSCConcurrentFifoOrder) {
    // SPSC 场景：单生产者单消费者，验证 FIFO 顺序
    constexpr std::size_t kMessages = 1000;
    RingBuffer<4096> rb;

    std::thread producer([&] {
        for (std::size_t i = 0; i < kMessages; ++i) {
            std::string s = "msg-" + std::to_string(i);
            auto data = bytes_of(s);
            while (rb.write(data).is_err()) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<std::string> consumed;
    consumed.reserve(kMessages);
    std::thread consumer([&] {
        for (std::size_t i = 0; i < kMessages; ++i) {
            std::array<std::byte, 64> out{};
            udaf::core::Result<std::size_t> r =
                udaf::core::Result<std::size_t>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
            while (r.is_err()) {
                r = rb.read(out);
                if (r.is_err()) std::this_thread::yield();
            }
            std::string s(reinterpret_cast<const char*>(out.data()), r.value());
            consumed.push_back(s);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), kMessages);
    for (std::size_t i = 0; i < kMessages; ++i) {
        EXPECT_EQ(consumed[i], "msg-" + std::to_string(i))
            << "FIFO order violated at i=" << i;
    }
}

TEST(DynamicRingBuffer, BasicRoundTrip) {
    DynamicRingBuffer rb(1024);
    auto data = bytes_of("dynamic");
    ASSERT_TRUE(rb.write(data).is_ok());
    EXPECT_EQ(rb.size(), data.size() + sizeof(uint32_t));
    EXPECT_FALSE(rb.empty());

    std::vector<std::byte> out(64);
    auto r = rb.read(out);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), data.size());
    EXPECT_EQ(std::memcmp(out.data(), data.data(), data.size()), 0);
    EXPECT_TRUE(rb.empty());
}

TEST(DynamicRingBuffer, FreeSpaceEmpty) {
    DynamicRingBuffer rb(1024);
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.free_space(), 1024u);
}

TEST(DynamicRingBuffer, WriteTooLargeReturnsErr) {
    DynamicRingBuffer rb(64);
    std::vector<std::byte> big(64, std::byte{0});
    auto r = rb.write(big);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
}

TEST(DynamicRingBuffer, WriteFullReturnsErr) {
    DynamicRingBuffer rb(64);
    // 写 5 条 8B 数据（含 4B header 各 = 12B 实际，5*12=60B 剩余 4B）
    std::vector<std::byte> msg(8, std::byte{1});
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(rb.write(msg).is_ok());
    }
    // 第 6 条 12B 写入时只剩 4B，应返回 RES_MEMORY_EXHAUSTED
    auto r = rb.write(msg);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::RES_MEMORY_EXHAUSTED);
}

TEST(DynamicRingBuffer, ReadEmptyReturnsErr) {
    DynamicRingBuffer rb(64);
    std::vector<std::byte> out(16);
    auto r = rb.read(out);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::RES_MEMORY_EXHAUSTED);
}

TEST(DynamicRingBuffer, ReadTooSmallBufferReturnsErr) {
    DynamicRingBuffer rb(256);
    std::vector<std::byte> big(100, std::byte{0xAB});
    ASSERT_TRUE(rb.write(big).is_ok());
    std::vector<std::byte> out(50);  // < 100
    auto r = rb.read(out);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
}

TEST(DynamicRingBuffer, CrossBoundaryWrite) {
    DynamicRingBuffer rb(256);
    // 先填一些再消费，强制 head 位于非零位置
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(rb.write(bytes_of("a")).is_ok());
        std::vector<std::byte> out(8);
        ASSERT_TRUE(rb.read(out).is_ok());
    }
    // 跨边界写入一条较长记录
    std::string payload(120, 'b');
    ASSERT_TRUE(rb.write(bytes_of(payload)).is_ok());
    std::vector<std::byte> out(140);
    auto r = rb.read(out);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 120u);
    EXPECT_EQ(std::memcmp(out.data(), payload.data(), 120), 0);
}

TEST(DynamicRingBuffer, MultipleReadWriteCycle) {
    DynamicRingBuffer rb(256);
    for (int i = 0; i < 20; ++i) {
        std::string s = "msg-" + std::to_string(i);
        ASSERT_TRUE(rb.write(bytes_of(s)).is_ok());
    }
    for (int i = 0; i < 20; ++i) {
        std::vector<std::byte> out(32);
        auto r = rb.read(out);
        ASSERT_TRUE(r.is_ok());
        std::string got(reinterpret_cast<const char*>(out.data()), r.value());
        EXPECT_EQ(got, "msg-" + std::to_string(i));
    }
    EXPECT_TRUE(rb.empty());
}