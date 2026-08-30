// nodes.cpp - 业务节点实现
#include "nodes.hpp"

#include "ability_b/port/port.hpp"

#include <chrono>
#include <utility>

namespace udaf::ability_c::nodes {

// ---------------- CmdExecNode ----------------

CmdExecNode::CmdExecNode()
    : Node("cmd_exec"),
      in_cmd_("cmd_in", 64),
      out_result_(ability_b::port::PortInfo{"cmd_out",
                  std::type_index(typeid(messages::CmdResult)), 1, false},
                  nullptr) {
    inputs_.push_back(in_cmd_.info());
    outputs_.push_back(out_result_.info());
}

CmdExecNode::~CmdExecNode() { stop(); }

void CmdExecNode::set_allowed_executables(std::vector<std::string> a) noexcept {
    allowed_ = std::move(a);
}

core::Result<void>
CmdExecNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    worker_ = std::thread([this] { worker(); });
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::stop() noexcept {
    if (!running_.exchange(false)) {
        set_state(ability_b::node::LifecycleState::Stopped);
        return core::Result<void>::ok();
    }
    if (worker_.joinable()) worker_.join();
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

void CmdExecNode::worker() noexcept {
    while (running_.load()) {
        auto r = in_cmd_.recv(100);
        if (r.is_err()) continue;
        const auto& req = r.value();
        executor::ProcessExecutor::Options opts;
        opts.executable = req.command;
        opts.args = req.args;
        opts.allowed_executables = allowed_;
        auto er = executor::ProcessExecutor::execute(opts);
        messages::CmdResult res;
        if (er.is_ok()) {
            res.exit_code = er.value().exit_code;
            res.stdout_text = std::move(er.value().stdout_text);
            res.stderr_text = std::move(er.value().stderr_text);
            res.elapsed_ns = er.value().elapsed_ns;
        } else {
            res.exit_code = -1;
            res.stderr_text = "exec failed";
        }
        (void)out_result_.try_send(std::move(res));
    }
}

// ---------------- HeartbeatNode ----------------

HeartbeatNode::HeartbeatNode()
    : Node("heartbeat"),
      in_hb_("hb_in", 1024),
      out_hb_(ability_b::port::PortInfo{"hb_out",
                std::type_index(typeid(messages::Heartbeat)), 1, false},
                nullptr) {
    inputs_.push_back(in_hb_.info());
    outputs_.push_back(out_hb_.info());
}

HeartbeatNode::~HeartbeatNode() = default;

core::Result<void>
HeartbeatNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::start() noexcept {
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::stop() noexcept {
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

}  // namespace udaf::ability_c::nodes