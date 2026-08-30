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

}  // namespace udaf::ability_c::nodes

#endif  // UDAF_ABILITY_C_NODES_NODES_HPP