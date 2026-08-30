// port.hpp - 数据流端口（强类型 + Rule of Five + 缓存 PortInfo）
//
// 设计要点（评审 C-4）：
//   - InputPort<T> / OutputPort<T> 强类型
//   - 缓存 PortInfo 成员（info() 返回 const PortInfo&）
//   - Rule of Five：=delete 拷贝，noexcept 移动
//   - try_recv / try_send 满/空时返回 BUSY

#ifndef UDAF_ABILITY_B_PORT_PORT_HPP
#define UDAF_ABILITY_B_PORT_PORT_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <typeindex>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_b::port {

/// 端口元数据
struct PortInfo {
    std::string name;
    std::type_index type_index_ = std::type_index(typeid(void));
    std::uint32_t schema_version = 0;
    bool is_input = false;

    [[nodiscard]] bool is_output() const noexcept { return !is_input; }
};

/// 输入端口（强类型 + 缓存 PortInfo + 内置 SPSC/FIFO）
template <typename T>
class InputPort {
public:
    /// @param name 端口名（用于诊断）
    /// @param capacity 内部队列容量（0 = 无限）
    /// @param schema_version 类型 schema 版本号
    explicit InputPort(std::string name, std::size_t capacity = 64,
                       std::uint32_t schema_version = 1) noexcept
        : info_{std::move(name), std::type_index(typeid(T)), schema_version, true},
          capacity_(capacity) {}

    ~InputPort() = default;
    InputPort(const InputPort&) = delete;
    InputPort& operator=(const InputPort&) = delete;
    InputPort(InputPort&&) noexcept = default;
    InputPort& operator=(InputPort&&) noexcept = default;

    /// 端口元数据（const 引用，评审 C-4）
    [[nodiscard]] const PortInfo& info() const noexcept { return info_; }

    /// 尝试取出一条
    /// @return Ok(value) 或 Err(BIZ_SERVICE_NOT_FOUND)（队列空）
    [[nodiscard]] core::Result<T> try_recv() noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.empty()) {
            return core::Result<T>::err(core::ErrorCode::BIZ_SERVICE_NOT_FOUND);
        }
        T v = std::move(queue_.front());
        queue_.pop_front();
        return core::Result<T>::ok(std::move(v));
    }

    /// 阻塞等待（timeout_ms=-1 永久阻塞，0 非阻塞）
    [[nodiscard]] core::Result<T> recv(int timeout_ms) noexcept {
        std::unique_lock<std::mutex> lk(mtx_);
        if (queue_.empty() && timeout_ms != 0) {
            if (timeout_ms < 0) {
                cv_.wait(lk, [&] { return !queue_.empty(); });
            } else {
                cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                              [&] { return !queue_.empty(); });
            }
        }
        if (queue_.empty()) {
            return core::Result<T>::err(core::ErrorCode::NET_TIMEOUT);
        }
        T v = std::move(queue_.front());
        queue_.pop_front();
        return core::Result<T>::ok(std::move(v));
    }

    /// 当前大小
    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return queue_.size();
    }

    /// 容量
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// 内部 push（output_port → input_port 用）
    /// @return Err(RESOURCE_BUSY) 当 capacity > 0 且已满
    [[nodiscard]] core::Result<void> push(T v) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        if (capacity_ > 0 && queue_.size() >= capacity_) {
            return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
        }
        queue_.push_back(std::move(v));
        cv_.notify_one();
        return core::Result<void>::ok();
    }

private:
    PortInfo info_;
    std::size_t capacity_;
    std::deque<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};

/// 输出端口
template <typename T>
class OutputPort {
public:
    /// @param info 端口元数据
    /// @param target 对端输入端口（直接关联，简化版；后续支持 Channel 中转）
    explicit OutputPort(PortInfo info, InputPort<T>* target) noexcept
        : info_(std::move(info)), target_(target) {}

    ~OutputPort() = default;
    OutputPort(const OutputPort&) = delete;
    OutputPort& operator=(const OutputPort&) = delete;
    OutputPort(OutputPort&&) noexcept = default;
    OutputPort& operator=(OutputPort&&) = default;

    [[nodiscard]] const PortInfo& info() const noexcept { return info_; }

    /// 尝试发送
    [[nodiscard]] core::Result<void> try_send(T v) noexcept {
        if (!target_) {
            return core::Result<void>::err(core::ErrorCode::NET_NOT_CONNECTED);
        }
        return target_->push(std::move(v));
    }

private:
    PortInfo info_;
    InputPort<T>* target_ = nullptr;
};

}  // namespace udaf::ability_b::port

#endif  // UDAF_ABILITY_B_PORT_PORT_HPP