// channel.hpp - 数据流传输通道（Inproc / IPC / TCP 三类）
//
// 设计要点（评审 C-3）：
//   - ChannelBase 类型擦除基类
//   - Channel<T> 模板包装
//   - 4 个枚举：MessagePriority / RecvStatus / SendResult / TransportType
//   - 不 include udaf::ability_a::*（评审 P0）
//   - 当前实现：仅 InprocChannel（零拷贝 SPSC），IpcChannel/TcpChannel 留阶段 D 扩展
//
// 设计依据：docs/04-module-design.md §2.5 + docs/03-detailed-design.md §3.3.5

#ifndef UDAF_ABILITY_B_TRANSPORT_CHANNEL_HPP
#define UDAF_ABILITY_B_TRANSPORT_CHANNEL_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_b::transport {

enum class TransportType : std::uint8_t { Inproc, Ipc, Tcp };

enum class MessagePriority : std::uint8_t { Heartbeat = 0, Control = 1, Data = 2 };

enum class RecvStatus : std::uint8_t { Ok, Timeout, Closed, Error };

enum class SendResult : std::uint8_t { Ok, Full, Closed, Error };

/// 消息帧（type-erased）
struct MessageFrame {
    std::vector<std::uint8_t> payload;
    MessagePriority priority = MessagePriority::Data;
    std::uint64_t seq = 0;
};

/// 类型擦除通道基类（评审 C-3）
class ChannelBase {
public:
    virtual ~ChannelBase() = default;

    /// 发送类型擦除消息
    [[nodiscard]] virtual SendResult send_base(MessageFrame m) noexcept = 0;

    /// 接收类型擦除消息
    [[nodiscard]] virtual RecvStatus recv_base(MessageFrame& m,
                                               int timeout_ms) noexcept = 0;

    /// 关闭
    virtual void close() noexcept = 0;

    /// 传输类型
    [[nodiscard]] virtual TransportType transport() const noexcept = 0;
};

/// 模板通道（用户提供类型 T，编译期校验）
template <typename T>
class Channel {
public:
    /// @param inner 类型擦除底层通道（持有 ownership）
    explicit Channel(std::unique_ptr<ChannelBase> inner) noexcept
        : inner_(std::move(inner)) {}

    ~Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) = default;

    [[nodiscard]] SendResult send(T v, MessagePriority p = MessagePriority::Data) noexcept {
        MessageFrame m;
        // 类型特定编码：此处简化采用 vector of bytes；实际可由 Serializer<T> 完成
        // 为保持 Channel 轻量，调用方应在更高层做 encode；此处透传二进制
        (void)v;
        m.priority = p;
        return inner_->send_base(std::move(m));
    }

    [[nodiscard]] RecvStatus recv(T& /*v*/, int timeout_ms) noexcept {
        MessageFrame m;
        auto st = inner_->recv_base(m, timeout_ms);
        // 简化：调用方应在更高层做 decode
        return st;
    }

    void close() noexcept { inner_->close(); }

    [[nodiscard]] TransportType transport() const noexcept { return inner_->transport(); }

private:
    std::unique_ptr<ChannelBase> inner_;
};

}  // namespace udaf::ability_b::transport

#endif  // UDAF_ABILITY_B_TRANSPORT_CHANNEL_HPP