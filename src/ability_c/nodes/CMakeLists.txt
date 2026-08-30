# src/ability_c/nodes 模块 - 阶段 D4（4 个业务节点）
# 详细设计见 docs/04-module-design.md §2.11 + docs/03-detailed-design.md §4
#
# 公开 API（4 个继承 udaf::ability_b::node::Node 的派生类）：
#   udaf::ability_c::nodes::CmdExecNode    (InputPort<CmdRequest> + OutputPort<CmdResult>)
#   udaf::ability_c::nodes::FileXferNode   (InputPort<FileChunk> + OutputPort<FileAck>)
#   udaf::ability_c::nodes::HeartbeatNode  (输入 100 设备心跳 → 聚合输出)
#   udaf::ability_c::nodes::NetInfoNode    (原 net_interface_node 改名 D-1)

add_library(udaf_ability_c_nodes STATIC
    nodes.cpp
)

target_include_directories(udaf_ability_c_nodes
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_c_nodes
    PUBLIC
        udaf::udaf_core
        udaf::udaf_ability_b_node
        udaf::udaf_ability_b_port
        udaf::udaf_ability_b_serialization
        udaf::udaf_ability_c_executor
        udaf::udaf_ability_c_messages
        Threads::Threads
)

target_compile_features(udaf_ability_c_nodes PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_c_nodes ALIAS udaf_ability_c_nodes)