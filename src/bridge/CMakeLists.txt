# src/bridge 模块 - 阶段 B4（顶层回调接口）
# 详细设计见 docs/03-detailed-design.md §2.3.3 + §3.3.2（评审 P0）
#
# 公开 API：
#   udaf::bridge::TopologyUpdateCallbacks  (纯抽象 std::function 注入)
#   - 不持有任何 udaf::ability_b::* 类型（评审 P0：A 不 include B）

add_library(udaf_bridge INTERFACE)

target_include_directories(udaf_bridge
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(udaf_bridge
    INTERFACE
        udaf::udaf_core
)

target_compile_features(udaf_bridge INTERFACE cxx_std_20)

add_library(udaf::udaf_bridge ALIAS udaf_bridge)