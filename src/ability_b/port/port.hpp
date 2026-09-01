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

/// @brief 端口元数据，描述单个端口的名称、承载类型与方向。
///
/// 缓存于 InputPort / OutputPort 内部，外部通过 info() const& 读取，
/// 避免每次查询触发 std::type_index 重新构造。
struct PortInfo {
    std::string name;                          ///< 端口名（用于诊断与日志）
    std::type_index type_index_ = std::type_index(typeid(void));  ///< C++ 类型擦除（std::type_index）
    std::uint32_t schema_version = 0;         ///< 关联 protobuf / 业务 schema 版本号
    bool is_input = false;                     ///< true = 输入端口；false = 输出端口

    /// @brief 判断是否为输出端口（与 is_input 互斥）。
    [[nodiscard]] bool is_output() const noexcept { return !is_input; }
};

/// @brief 输入端口，强类型模板 + 内置线程安全 FIFO 队列。
///
/// 缓存 PortInfo 成员（评审 C-4 修复），支持 try_recv 非阻塞与
/// recv 带超时的阻塞两种取数据模式。HEARTBEAT 始终强制投递：
/// 由 OutputPort::try_send 走 push()，capacity 满时返回 RESOURCE_BUSY
/// 而非丢消息（架构 §5.6）。
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

    /// @brief 返回端口元数据（const 引用，无拷贝）。
    [[nodiscard]] const PortInfo& info() const noexcept { return info_; }

    /// @brief 尝试取出一条消息（非阻塞）。
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

    /// @brief 阻塞等待直到队列非空或超时。
    /// @param timeout_ms 超时毫秒数；-1 = 永久阻塞；0 = 非阻塞（等价 try_recv）
    /// @return Ok(value) 或 Err(NET_TIMEOUT)（超时且仍为空）
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

    /// @brief 当前队列长度（线程安全）。
    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        return queue_.size();
    }

    /// @brief 队列容量（0 表示无界）。
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// @brief 推入一条消息（通常由 OutputPort::try_send 调用）。
    /// @param v 要推入的值（左值或右值均可，内部移动）
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

/// @brief 输出端口，持有对端 InputPort 指针，转发消息到目标队列。
///
/// 简化版直接关联 InputPort*；后续可扩展支持 Channel<T> 中转。
/// HEARTBEAT 优先级由 try_send 自动走 capacity 检查，capacity=0 时无限。
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

    /// @brief 尝试向对端发送一条消息。
    /// @param v 要发送的值
    /// @return Err(NET_NOT_CONNECTED) 当 target_ 为空；
    ///         Err(RESOURCE_BUSY) 当对端队列满
    [[nodiscard]] core::Result<void> try_send(T v) noexcept {
        if (!target_) {
            return core::Result<void>::err(core::ErrorCode::NET_NOT_CONNECTED);
        }
        return target_->push(std::move(v));
    }

    /// @brief 运行时绑定/解绑对端 InputPort（仅测试 / SDK 编排使用）。
    /// 生产拓扑中 target_ 在构造期注入，节点生命周期内不变。
    void set_target(InputPort<T>* p) noexcept { target_ = p; }
    [[nodiscard]] InputPort<T>* target() const noexcept { return target_; }

private:
    PortInfo info_;
    InputPort<T>* target_ = nullptr;
};

}  // namespace udaf::ability_b::port

#endif  // UDAF_ABILITY_B_PORT_PORT_HPP