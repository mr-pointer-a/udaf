# src/ability_a/bridge 模块 - 阶段 D2（DiscoveryBridge 实现）
# 详细设计见 docs/03-detailed-design.md §2.3.3（评审 P0：DiscoveryBridge 通过 udaf::bridge 注入）
# 关键约束：DiscoveryBridge 不 include udaf::ability_b::* 任何头

add_library(udaf_ability_a_bridge STATIC
    discovery_bridge.cpp
)

target_include_directories(udaf_ability_a_bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_a_bridge
    PUBLIC
        udaf::udaf_core
        udaf::udaf_bridge
)

target_compile_features(udaf_ability_a_bridge PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_a_bridge ALIAS udaf_ability_a_bridge)