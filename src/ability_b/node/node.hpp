// node.hpp - 数据流节点基类 + Scheduler + Lifecycle
//
// 设计要点（评审 C-1 / C-3）：
//   - Node::inputs() / outputs() 返回 const vector<PortInfo>&（评审 C-3）
//   - Scheduler 通过回调注入 WhitelistCheck（评审 P0：不持有 PeerWhitelist&）
//   - Lifecycle 状态机：INIT → STARTING → RUNNING → RELOADING → STOPPING → STOPPED
//   - Node::init(Config) 返回 Result<void>（不抛异常）
//
// 设计依据：docs/04-module-design.md §2.4 + docs/03-detailed-design.md §3.3.1

#ifndef UDAF_ABILITY_B_NODE_NODE_HPP
#define UDAF_ABILITY_B_NODE_NODE_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"
#include "ability_b/port/port.hpp"

namespace udaf::ability_b::node {

/// 节点配置（最小集）
struct NodeConfig {
    std::string node_id;
    std::uint32_t worker_threads = 1;
    std::uint64_t rate_limit_per_sec = 0;  // 0 = 无限
};

/// Lifecycle 状态（评审 C-1：6 状态）
enum class LifecycleState : std::uint8_t {
    Init      = 0,
    Starting  = 1,
    Running   = 2,
    Reloading = 3,
    Stopping  = 4,
    Stopped   = 5,
};

[[nodiscard]] inline const char* to_string(LifecycleState s) noexcept {
    switch (s) {
        case LifecycleState::Init:      return "INIT";
        case LifecycleState::Starting:  return "STARTING";
        case LifecycleState::Running:   return "RUNNING";
        case LifecycleState::Reloading: return "RELOADING";
        case LifecycleState::Stopping:  return "STOPPING";
        case LifecycleState::Stopped:   return "STOPPED";
    }
    return "UNKNOWN";
}

/// 节点抽象基类
class Node {
public:
    explicit Node(std::string name) noexcept : name_(std::move(name)) {}
    virtual ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    /// 初始化（子类实现）
    [[nodiscard]] virtual core::Result<void> init(const NodeConfig& cfg) noexcept = 0;

    /// 启动
    [[nodiscard]] virtual core::Result<void> start() noexcept = 0;

    /// 停止
    [[nodiscard]] virtual core::Result<void> stop() noexcept = 0;

    /// 重载（热更新）
    [[nodiscard]] virtual core::Result<void> reload() noexcept = 0;

    /// 输入端口元数据（const 引用）
    [[nodiscard]] virtual const std::vector<port::PortInfo>& inputs() const noexcept = 0;

    /// 输出端口元数据
    [[nodiscard]] virtual const std::vector<port::PortInfo>& outputs() const noexcept = 0;

    /// 当前生命周期
    [[nodiscard]] LifecycleState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /// 节点名
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

protected:
    void set_state(LifecycleState s) noexcept {
        state_.store(s, std::memory_order_release);
    }

private:
    std::string name_;
    std::atomic<LifecycleState> state_{LifecycleState::Init};
};

/// 白名单检查回调（签名：被调度节点 id + 调度源节点 id → 是否允许）
using WhitelistCheck =
    std::function<bool(std::string_view /*target*/, std::string_view /*source*/)>;

/// Scheduler：负责节点调度（评审 P0：通过回调注入白名单）
class Scheduler {
public:
    Scheduler() = default;

    void set_whitelist_check(WhitelistCheck cb) noexcept {
        whitelist_check_ = std::move(cb);
    }

    /// 调度 node 到 target_host（通过白名单检查）
    /// @return Err(WHITELIST_DENIED) 当回调返回 false
    [[nodiscard]] core::Result<void>
    schedule(const std::string& node_id, std::string_view target_host) noexcept;

    /// 检查是否允许
    [[nodiscard]] bool
    is_allowed(std::string_view target, std::string_view source) const noexcept;

private:
    WhitelistCheck whitelist_check_;
};

}  // namespace udaf::ability_b::node

#endif  // UDAF_ABILITY_B_NODE_NODE_HPP