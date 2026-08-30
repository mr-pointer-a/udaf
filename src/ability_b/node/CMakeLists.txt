# src/ability_b/node 模块 - 阶段 C5
# 详细设计见 docs/04-module-design.md §2.4 + docs/03-detailed-design.md §3.3.1
#
# 公开 API：
#   udaf::ability_b::node::Node (抽象基类)
#   udaf::ability_b::node::Scheduler (含 using Coordinator = Scheduler; + WhitelistCheck 回调)
#   udaf::ability_b::node::LifecycleState (6 状态)

add_library(udaf_ability_b_node STATIC
    node.cpp
)

target_include_directories(udaf_ability_b_node
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_b_node
    PUBLIC
        udaf::udaf_core
        udaf::udaf_ability_b_port
)

target_compile_features(udaf_ability_b_node PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_b_node ALIAS udaf_ability_b_node)