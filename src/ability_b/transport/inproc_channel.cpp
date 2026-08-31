// inproc_channel.cpp - InprocChannel 实现
#include "inproc_channel.hpp"

#include <chrono>
#include <utility>

namespace udaf::ability_b::transport {

InprocChannel::InprocChannel(std::size_t capacity_per_priority) noexcept
    : capacity_(capacity_per_priority) {}

InprocChannel::~InprocChannel() {
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    // 析构阶段调用 close() 是有意为之（基类析构前必须先关闭队列）。
    // 虚分派在此处按值进行是安全的，因为 InprocChannel 已是最末派生类。
    close();
}

SendResult InprocChannel::send_base(MessageFrame m) noexcept {
    if (closed_) return SendResult::Closed;
    const auto prio = static_cast<std::size_t>(m.priority);
    if (prio >= queues_.size()) return SendResult::Error;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // HEARTBEAT 永远不被丢弃（架构 §5.6）
        if (m.priority != MessagePriority::Heartbeat &&
            queues_[prio].size() >= capacity_) {
            return SendResult::Full;
        }
        m.seq = next_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
        queues_[prio].push_back(std::move(m));
    }
    cv_.notify_one();
    return SendResult::Ok;
}

RecvStatus InprocChannel::recv_base(MessageFrame& m, int timeout_ms) noexcept {
    std::unique_lock<std::mutex> lk(mtx_);
    // 优先级：Heartbeat > Control > Data
    auto pick = [&]() -> bool {
        for (auto& q : queues_) {
            if (!q.empty()) {
                m = std::move(q.front());
                q.pop_front();
                return true;
            }
        }
        return false;
    };

    if (!pick()) {
        if (timeout_ms == 0) return RecvStatus::Timeout;
        if (closed_) return RecvStatus::Closed;
        if (timeout_ms < 0) {
            cv_.wait(lk, [&] { return closed_ || pick(); });
        } else {
            cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                          [&] { return closed_ || pick(); });
        }
        if (closed_ && queues_[0].empty() &&
            queues_[1].empty() && queues_[2].empty()) {
            return RecvStatus::Closed;
        }
        if (!pick()) return RecvStatus::Timeout;
    }
    return RecvStatus::Ok;
}

void InprocChannel::close() noexcept {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        closed_ = true;
    }
    cv_.notify_all();
}

}  // namespace udaf::ability_b::transport