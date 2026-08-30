// topology_update_callbacks.hpp - 顶层回调接口
//
// 设计要点（评审 P0）：
//   - 纯 std::function 注入，不持有任何 udaf::ability_b::* 类型
//   - 用于解耦 ability_a::discovery（注册事件源）与 ability_b::topology（消费事件）
//   - A 不 include B（避免循环依赖）

#ifndef UDAF_BRIDGE_TOPOLOGY_UPDATE_CALLBACKS_HPP
#define UDAF_BRIDGE_TOPOLOGY_UPDATE_CALLBACKS_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace udaf::bridge {

/// 节点加入事件
struct NodeJoinEvent {
    std::string node_id;
    std::string hostname;
    std::string bind_address;
    std::uint16_t bind_port;
    std::vector<std::string> services;
};

/// 节点离开事件
struct NodeLeaveEvent {
    std::string node_id;
};

/// 拓扑更新回调集（由 ability_b::topology 实现，注入给 ability_a::discovery）
class TopologyUpdateCallbacks {
public:
    virtual ~TopologyUpdateCallbacks() = default;

    /// 节点加入（discovery → topology）
    virtual void on_node_join(const NodeJoinEvent& ev) noexcept = 0;

    /// 节点离开
    virtual void on_node_leave(const NodeLeaveEvent& ev) noexcept = 0;

    /// 节点心跳续约（可选实现）
    virtual void on_node_heartbeat(std::string_view /*node_id*/) noexcept {}
};

}  // namespace udaf::bridge

#endif  // UDAF_BRIDGE_TOPOLOGY_UPDATE_CALLBACKS_HPP