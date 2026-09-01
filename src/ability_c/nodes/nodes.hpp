// nodes.hpp - 4 个业务节点（CmdExec / FileXfer / Heartbeat / NetInfo）
//
// 设计要点：
//   - 全部继承 udaf::ability_b::node::Node
//   - 强类型端口（InputPort<CmdRequest> 等）
//   - 不抛异常

#ifndef UDAF_ABILITY_C_NODES_NODES_HPP
#define UDAF_ABILITY_C_NODES_NODES_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ability_b/node/node.hpp"
#include "ability_b/port/port.hpp"
#include "ability_c/executor/process_executor.hpp"
#include "ability_c/messages/messages.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_c::nodes {

// ---------------- CmdExecNode ----------------

class CmdExecNode : public ability_b::node::Node {
public:
    CmdExecNode();
    ~CmdExecNode() override;

    core::Result<void> init(const ability_b::node::NodeConfig& cfg) noexcept override;
    core::Result<void> start() noexcept override;
    core::Result<void> stop() noexcept override;
    core::Result<void> reload() noexcept override;

    const std::vector<ability_b::port::PortInfo>& inputs() const noexcept override { return inputs_; }
    const std::vector<ability_b::port::PortInfo>& outputs() const noexcept override { return outputs_; }

    /// 注入命令白名单
    void set_allowed_executables(std::vector<std::string> allow) noexcept;

    /// 端口访问器（供测试触发 worker 路径）
    ability_b::port::InputPort<messages::CmdRequest>& in_cmd() noexcept { return in_cmd_; }
    ability_b::port::OutputPort<messages::CmdResult>& out_result() noexcept { return out_result_; }

private:
    void worker() noexcept;

    ability_b::port::InputPort<messages::CmdRequest>  in_cmd_;
    ability_b::port::OutputPort<messages::CmdResult> out_result_;
    std::vector<ability_b::port::PortInfo> inputs_;
    std::vector<ability_b::port::PortInfo> outputs_;

    std::vector<std::string> allowed_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

// ---------------- HeartbeatNode ----------------

class HeartbeatNode : public ability_b::node::Node {
public:
    HeartbeatNode();
    ~HeartbeatNode() override;

    core::Result<void> init(const ability_b::node::NodeConfig& cfg) noexcept override;
    core::Result<void> start() noexcept override;
    core::Result<void> stop() noexcept override;
    core::Result<void> reload() noexcept override;

    const std::vector<ability_b::port::PortInfo>& inputs() const noexcept override { return inputs_; }
    const std::vector<ability_b::port::PortInfo>& outputs() const noexcept override { return outputs_; }

private:
    ability_b::port::InputPort<messages::Heartbeat>  in_hb_;
    ability_b::port::OutputPort<messages::Heartbeat> out_hb_;
    std::vector<ability_b::port::PortInfo> inputs_;
    std::vector<ability_b::port::PortInfo> outputs_;
};

// ---------------- FileXferNode ----------------

/// 分块文件传输节点：接收 FileChunk 写入磁盘，输出 FileAck。
/// 设计：
///   - 单线程 worker（写盘 IO 串行）
///   - 路径白名单由 set_allowed_paths() 注入
///   - 拒绝 ../ 路径穿越、符号链接外跳（评审 C-2 修复）
///   - is_last=true 时关闭文件描述符并返回 ok=true
class FileXferNode : public ability_b::node::Node {
public:
    FileXferNode();
    ~FileXferNode() override;

    core::Result<void> init(const ability_b::node::NodeConfig& cfg) noexcept override;
    core::Result<void> start() noexcept override;
    core::Result<void> stop() noexcept override;
    core::Result<void> reload() noexcept override;

    const std::vector<ability_b::port::PortInfo>& inputs() const noexcept override { return inputs_; }
    const std::vector<ability_b::port::PortInfo>& outputs() const noexcept override { return outputs_; }

    /// 注入允许的根目录（多个）；只允许写入这些根之下的文件
    void set_allowed_paths(std::vector<std::string> roots) noexcept;

    ability_b::port::InputPort<messages::FileChunk>& in_chunk() noexcept { return in_chunk_; }
    ability_b::port::OutputPort<messages::FileAck>& out_ack() noexcept { return out_ack_; }

private:
    /// 校验路径是否在白名单内（且不包含 ../）
    /// 路径若以任一 allowed_root_ 前缀开头则允许（绝对/相对均可）；
    /// 未配置白名单时放行（开发态）。
    [[nodiscard]] bool path_allowed(const std::string& p) const noexcept;
    /// 写一块数据到文件，返回成功字节数（-1 表示失败）
    [[nodiscard]] std::int64_t write_block(const std::string& path,
                                            std::uint64_t offset,
                                            const std::vector<std::uint8_t>& data) noexcept;
    /// worker 线程主循环
    void worker() noexcept;

    ability_b::port::InputPort<messages::FileChunk>  in_chunk_;
    ability_b::port::OutputPort<messages::FileAck>    out_ack_;
    std::vector<ability_b::port::PortInfo> inputs_;
    std::vector<ability_b::port::PortInfo> outputs_;

    std::vector<std::string> allowed_roots_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

// ---------------- NetInfoNode ----------------

/// 网络接口查询/设置节点：
///   - 输入 NetInterfaceQuery → 输出 NetInterfaceResult
///   - 输入 NetInterfaceSet → 输出 NetInterfaceResult（应用 set 后查询）
/// 实现：直接读 /sys/class/net，简化处理（生产环境应走 netlink）。
class NetInfoNode : public ability_b::node::Node {
public:
    NetInfoNode();
    ~NetInfoNode() override;

    core::Result<void> init(const ability_b::node::NodeConfig& cfg) noexcept override;
    core::Result<void> start() noexcept override;
    core::Result<void> stop() noexcept override;
    core::Result<void> reload() noexcept override;

    const std::vector<ability_b::port::PortInfo>& inputs() const noexcept override { return inputs_; }
    const std::vector<ability_b::port::PortInfo>& outputs() const noexcept override { return outputs_; }

    /// 端口访问器（供测试触发 worker 路径）
    ability_b::port::InputPort<messages::NetInterfaceQuery>& in_query() noexcept { return in_query_; }
    ability_b::port::InputPort<messages::NetInterfaceSet>& in_set() noexcept { return in_set_; }
    ability_b::port::OutputPort<messages::NetInterfaceResult>& out_result() noexcept { return out_result_; }

    /// 注入结果接收端（仅测试 / SDK 编排使用；生产环境由 Scheduler 注入）
    void bind_result_target(ability_b::port::InputPort<messages::NetInterfaceResult>* p) noexcept {
        out_result_.set_target(p);
    }

private:
    /// 查询指定网卡（ifname 为空 = 全部）
    [[nodiscard]] messages::NetInterfaceResult query_one(const std::string& ifname) noexcept;
    /// 应用 NetInterfaceSet（up/down）
    [[nodiscard]] messages::NetInterfaceResult apply_set(const messages::NetInterfaceSet& s) noexcept;

    ability_b::port::InputPort<messages::NetInterfaceQuery> in_query_;
    ability_b::port::InputPort<messages::NetInterfaceSet>   in_set_;
    ability_b::port::OutputPort<messages::NetInterfaceResult> out_result_;
    std::vector<ability_b::port::PortInfo> inputs_;
    std::vector<ability_b::port::PortInfo> outputs_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace udaf::ability_c::nodes

#endif  // UDAF_ABILITY_C_NODES_NODES_HPP