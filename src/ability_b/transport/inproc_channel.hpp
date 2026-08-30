// inproc_channel.hpp - 进程内 SPSC 通道（零拷贝）
//
// 设计要点：
//   - 双端 SPSC：无锁单生产者单消费者
//   - 队列基于 core::RingBuffer（零拷贝）
//   - 优先级：3 级子队列
//   - HEARTBEAT 永远不被丢弃（架构 §5.6）

#ifndef UDAF_ABILITY_B_TRANSPORT_INPROC_CHANNEL_HPP
#define UDAF_ABILITY_B_TRANSPORT_INPROC_CHANNEL_HPP

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "channel.hpp"
#include "core/buffer/buffer.hpp"
#include "core/result.hpp"

namespace udaf::ability_b::transport {

/// InprocChannel：单进程 SPSC 通道（PIMPL 持有底层 deque）
class InprocChannel : public ChannelBase {
public:
    /// @param capacity_per_priority 每优先级子队列容量
    explicit InprocChannel(std::size_t capacity_per_priority = 1024) noexcept;
    ~InprocChannel() override;

    InprocChannel(const InprocChannel&) = delete;
    InprocChannel& operator=(const InprocChannel&) = delete;
    InprocChannel(InprocChannel&&) = delete;
    InprocChannel& operator=(InprocChannel&&) = delete;

    [[nodiscard]] SendResult send_base(MessageFrame m) noexcept override;
    [[nodiscard]] RecvStatus recv_base(MessageFrame& m, int timeout_ms) noexcept override;
    void close() noexcept override;
    [[nodiscard]] TransportType transport() const noexcept override {
        return TransportType::Inproc;
    }

private:
    using Queue = std::deque<MessageFrame>;

    std::array<Queue, 3> queues_;
    std::size_t capacity_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool closed_ = false;
    std::atomic<std::uint64_t> next_seq_{0};
};

}  // namespace udaf::ability_b::transport

#endif  // UDAF_ABILITY_B_TRANSPORT_INPROC_CHANNEL_HPP