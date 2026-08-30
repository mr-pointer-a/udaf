// buffer.hpp - UDAF 字节缓冲（SPSC 零拷贝环形队列）
//
// 设计要点（03 §6.1 + 04 §2.1）：
//   - 单生产者单消费者（SPSC）无锁环形缓冲
//   - 容量按 2 的幂对齐，便于位运算取模
//   - 支持多块 write/read（不要求单次完整）
//   - 不抛异常
//   - 线程安全：仅 SPSC 场景（多生产者/消费者需用 platform::sync::MpscQueue）
//
// 应用场景：
//   - ability_b::transport 内部 inproc 通道
//   - ability_a::discovery 扫描线程 → 主线程的事件传递
//   - 主线程 → I/O 线程的批写缓冲

#ifndef UDAF_CORE_BUFFER_BUFFER_HPP
#define UDAF_CORE_BUFFER_BUFFER_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "error_code.hpp"
#include "result.hpp"

namespace udaf::core {

/// SPSC 环形字节缓冲（单生产者单消费者，lock-free）。
///
/// 内存布局：
///   [reserved_head_=4B][data: capacity_ bytes]
///   reserved_head_ 持有本块长度（uint32_t, LE），write 时先写头再写数据。
///   实际容量为 capacity_ - sizeof(uint32_t)。
///
/// 不变量：
///   - head_ ∈ [tail_, head_ + capacity_]
///   - 数据全部按 4 字节对齐
template <std::size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "RingBuffer 容量必须是 2 的幂");

    /// 每条记录的额外头部开销（4B 长度）
    static constexpr std::size_t kEntryHeaderBytes = sizeof(std::uint32_t);

    /// 单条记录最大可用字节数
    static constexpr std::size_t kMaxEntryBytes = Capacity - kEntryHeaderBytes;

    RingBuffer() noexcept : head_(0), tail_(0) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /// 当前缓冲中的已用字节数（估算：head_ - tail_，mod 容量）。
    [[nodiscard]] std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_relaxed);
    }

    /// 剩余可用字节数。
    [[nodiscard]] std::size_t free_space() const noexcept {
        return Capacity - size();
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return free_space() < kEntryHeaderBytes + 1; }

    /// 单生产者：写入一段字节。
    /// @return Ok() 成功；Err(RES_MEMORY_EXHAUSTED) 缓冲已满
    [[nodiscard]] Result<void> write(std::span<const std::byte> data) noexcept {
        if (data.size() > kMaxEntryBytes) {
            return Result<void>::err(ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
        }
        const std::size_t need = kEntryHeaderBytes + data.size();
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        if (Capacity - (h - t) < need) {
            return Result<void>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
        }
        // 写长度头
        const std::uint32_t len = static_cast<std::uint32_t>(data.size());
        write_at(h, &len, kEntryHeaderBytes);
        // 写数据
        write_at(h + kEntryHeaderBytes, data.data(), data.size());
        // 发布 head
        head_.store(h + need, std::memory_order_release);
        return Result<void>::ok();
    }

    /// 单消费者：读出一条记录到 out。
    /// @param out 输出缓冲；长度 < kMaxEntryBytes 时返回 PROTOCOL_TRUNCATED_BUFFER
    /// @return Ok(写入 out 的字节数)；Err(RES_MEMORY_EXHAUSTED) 缓冲空 / 缓冲不足
    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> out) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (h == t) {
            return Result<std::size_t>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
        }
        std::uint32_t len = 0;
        read_at(t, &len, kEntryHeaderBytes);
        if (out.size() < len) {
            return Result<std::size_t>::err(ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
        }
        read_at(t + kEntryHeaderBytes, out.data(), len);
        tail_.store(t + kEntryHeaderBytes + len, std::memory_order_release);
        return Result<std::size_t>::ok(len);
    }

    /// 单消费者：仅查看下一条记录的长度（不消费）。
    [[nodiscard]] Result<std::uint32_t> peek_size() const noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (h == t) {
            return Result<std::uint32_t>::err(ErrorCode::RES_MEMORY_EXHAUSTED);
        }
        std::uint32_t len = 0;
        read_at(t, &len, kEntryHeaderBytes);
        return Result<std::uint32_t>::ok(len);
    }

private:
    /// 内部存储：环形缓冲区（大小为 Capacity）。
    alignas(64) std::array<std::byte, Capacity> storage_{};

    /// head_：生产者位置（仅 producer 写、producer 读 / consumer 读）
    alignas(64) std::atomic<std::size_t> head_{0};

    /// tail_：消费者位置（仅 consumer 写、consumer 读 / producer 读）
    alignas(64) std::atomic<std::size_t> tail_{0};

    /// 将逻辑位置映射到 storage 索引（位掩码取模）
    [[nodiscard]] static constexpr std::size_t index(std::size_t pos) noexcept {
        return pos & (Capacity - 1);
    }

    /// 在逻辑位置 pos 处写入 size 字节（可能跨边界，分两次写入）
    void write_at(std::size_t pos, const void* src, std::size_t size) noexcept {
        const std::size_t idx = index(pos);
        const std::size_t first = std::min(size, Capacity - idx);
        std::memcpy(storage_.data() + idx, src, first);
        if (first < size) {
            std::memcpy(storage_.data(), static_cast<const std::byte*>(src) + first,
                        size - first);
        }
    }

    /// 在逻辑位置 pos 处读出 size 字节
    void read_at(std::size_t pos, void* dst, std::size_t size) const noexcept {
        const std::size_t idx = index(pos);
        const std::size_t first = std::min(size, Capacity - idx);
        std::memcpy(dst, storage_.data() + idx, first);
        if (first < size) {
            std::memcpy(static_cast<std::byte*>(dst) + first, storage_.data(),
                        size - first);
        }
    }
};

/// 动态容量 SPSC 环形缓冲（运行时分配，适合容量需配置的链路）。
/// 接口与 RingBuffer<Capacity> 等价，仅容量来自构造函数。
class DynamicRingBuffer {
public:
    /// capacity 必须为 2 的幂
    explicit DynamicRingBuffer(std::size_t capacity);
    ~DynamicRingBuffer();

    DynamicRingBuffer(const DynamicRingBuffer&) = delete;
    DynamicRingBuffer& operator=(const DynamicRingBuffer&) = delete;
    DynamicRingBuffer(DynamicRingBuffer&&) = delete;
    DynamicRingBuffer& operator=(DynamicRingBuffer&&) = delete;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t free_space() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] Result<void> write(std::span<const std::byte> data) noexcept;
    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> out) noexcept;

private:
    std::size_t capacity_;
    std::vector<std::byte> storage_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};

    [[nodiscard]] static std::size_t index(std::size_t pos, std::size_t cap) noexcept {
        return pos & (cap - 1);
    }
};

}  // namespace udaf::core

#endif  // UDAF_CORE_BUFFER_BUFFER_HPP