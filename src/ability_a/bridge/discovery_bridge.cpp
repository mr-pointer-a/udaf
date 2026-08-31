// discovery_bridge.cpp - DiscoveryBridge 实现
#include "discovery_bridge.hpp"

namespace udaf::ability_a::bridge {

core::Result<void>
DiscoveryBridge::on_node_join(std::string node_id,
                              std::string hostname,
                              std::string bind_address,
                              std::uint16_t bind_port,
                              const std::vector<std::string>& services) noexcept {
    if (callbacks_ == nullptr) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    udaf::bridge::NodeJoinEvent ev;
    ev.node_id      = std::move(node_id);
    ev.hostname     = std::move(hostname);
    ev.bind_address = std::move(bind_address);
    ev.bind_port    = bind_port;
    ev.services     = services;
    callbacks_->on_node_join(ev);
    return core::Result<void>::ok();
}

core::Result<void>
DiscoveryBridge::on_node_leave(std::string node_id) noexcept {
    if (callbacks_ == nullptr) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    udaf::bridge::NodeLeaveEvent ev;
    ev.node_id = std::move(node_id);
    callbacks_->on_node_leave(ev);
    return core::Result<void>::ok();
}

void DiscoveryBridge::on_node_heartbeat(std::string_view node_id) noexcept {
    if (callbacks_ != nullptr) callbacks_->on_node_heartbeat(node_id);
}

}  // namespace udaf::ability_a::bridge