// discovery_bridge.hpp - DiscoveryBridge：discovery → topology 回调桥
//
// 关键约束（评审 P0）：不 include udaf::ability_b::* 任何头
// 通过 udaf::bridge::TopologyUpdateCallbacks 注入
//
// 设计依据：docs/03-detailed-design.md §2.3.3

#ifndef UDAF_ABILITY_A_BRIDGE_DISCOVERY_BRIDGE_HPP
#define UDAF_ABILITY_A_BRIDGE_DISCOVERY_BRIDGE_HPP

#include <memory>
#include <string>
#include <unordered_set>

#include "bridge/topology_update_callbacks.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::bridge {

/// DiscoveryBridge：将 discovery 事件映射到 topology 回调
class DiscoveryBridge {
public:
    /// @param callbacks topology 回调集（不持有所有权；调用方负责生命周期）
    explicit DiscoveryBridge(udaf::bridge::TopologyUpdateCallbacks* callbacks) noexcept
        : callbacks_(callbacks) {}

    /// 节点加入（调用方传入 node_id + hostname + bind_address/port + services）
    [[nodiscard]] core::Result<void>
    on_node_join(std::string node_id,
                 std::string hostname,
                 std::string bind_address,
                 std::uint16_t bind_port,
                 const std::vector<std::string>& services) noexcept;

    /// 节点离开
    [[nodiscard]] core::Result<void>
    on_node_leave(std::string node_id) noexcept;

    /// 心跳续约
    void on_node_heartbeat(std::string_view node_id) noexcept;

    /// 检测 callbacks 是否被注入
    [[nodiscard]] bool has_callbacks() const noexcept { return callbacks_ != nullptr; }

private:
    udaf::bridge::TopologyUpdateCallbacks* callbacks_ = nullptr;
};

}  // namespace udaf::ability_a::bridge

#endif  // UDAF_ABILITY_A_BRIDGE_DISCOVERY_BRIDGE_HPP