// buffer.cpp - DynamicRingBuffer 运行时实现
#include "buffer/buffer.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace udaf::core {

DynamicRingBuffer::DynamicRingBuffer(std::size_t capacity)
    : capacity_(capacity), storage_(capacity, std::byte{0}) {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        // 容量必须为 2 的幂；不抛异常，但通过 storage_ 大小不一致来反映错误
        assert(false && "DynamicRingBuffer 容量必须是 2 的幂");
    }
}

DynamicRingBuffer::~DynamicRingBuffer() = default;

std::size_t DynamicRingBuffer::size() const noexcept {
    return head_.load(std::memory_order_acquire) -
           tail_.load(std::memory_order_relaxed);
}

std::size_t DynamicRingBuffer::free_space() const noexcept {
    return capacity_ - size();
}

Result<void> DynamicRingBuffer::write(std::span<const std::byte> data) noexcept {
    static constexpr std::size_t kEntryHeaderBytes = sizeof(std::uint32_t);
    const std::size_t max_entry = capacity_ - kEntryHeaderBytes;
    if (data.size() > max_entry) {
        return Result<void>::err(ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
    }
    const std::size_t need = kEntryHeaderBytes + data.size();
    const std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_acquire);
    if (capacity_ - (h - t) < need) {
        return Result<void>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
    }
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    const std::size_t idx = index(h, capacity_);
    std::memcpy(storage_.data() + idx, &len, kEntryHeaderBytes);
    // 写入数据：单次 memcpy（data.size() < capacity_，可能跨边界）
    const std::size_t data_pos = (idx + kEntryHeaderBytes) & (capacity_ - 1);
    if (data_pos + data.size() <= capacity_) {
        std::memcpy(storage_.data() + data_pos, data.data(), data.size());
    } else {
        const std::size_t first = capacity_ - data_pos;
        std::memcpy(storage_.data() + data_pos, data.data(), first);
        std::memcpy(storage_.data(), data.data() + first, data.size() - first);
    }
    head_.store(h + need, std::memory_order_release);
    return Result<void>::ok();
}

Result<std::size_t> DynamicRingBuffer::read(std::span<std::byte> out) noexcept {
    static constexpr std::size_t kEntryHeaderBytes = sizeof(std::uint32_t);
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t h = head_.load(std::memory_order_acquire);
    if (h == t) {
        return Result<std::size_t>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
    }
    std::uint32_t len = 0;
    std::memcpy(&len, storage_.data() + index(t, capacity_), kEntryHeaderBytes);
    if (out.size() < len) {
        return Result<std::size_t>::err(ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
    }
    const std::size_t data_pos = index(t + kEntryHeaderBytes, capacity_);
    if (data_pos + len <= capacity_) {
        std::memcpy(out.data(), storage_.data() + data_pos, len);
    } else {
        // 跨边界复制：先复制尾部剩余，再从头部补齐
        const std::size_t first = capacity_ - data_pos;
        std::memcpy(out.data(), storage_.data() + data_pos, first);
        std::memcpy(out.data() + first, storage_.data(), len - first);
    }
    tail_.store(t + kEntryHeaderBytes + len, std::memory_order_release);
    return Result<std::size_t>::ok(len);
}

}  // namespace udaf::core