# src/ability_b/topology 模块 - 阶段 C4
# 详细设计见 docs/04-module-design.md §2.3 + docs/03-detailed-design.md §3.3.4
#
# 公开 API：
#   udaf::ability_b::topology::Topology
#   udaf::ability_b::topology::TopologyTransaction (独立类)
#   udaf::ability_b::topology::PeerNode / PeerEdge

add_library(udaf_ability_b_topology STATIC
    topology.cpp
)

target_include_directories(udaf_ability_b_topology
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_b_topology
    PUBLIC
        udaf::udaf_core
        udaf::udaf_platform_fs
        Threads::Threads
)

target_compile_features(udaf_ability_b_topology PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_b_topology ALIAS udaf_ability_b_topology)