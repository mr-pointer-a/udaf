# src/ability_b/port 模块 - 阶段 C2
# 详细设计见 docs/04-module-design.md §2.6 + docs/03-detailed-design.md §3.3.6（评审 C-4 修复）
#
# 公开 API：
#   udaf::ability_b::port::InputPort<T>  (强类型 + 缓存 PortInfo + Rule of Five)
#   udaf::ability_b::port::OutputPort<T>
#   udaf::ability_b::port::PortInfo

add_library(udaf_ability_b_port INTERFACE)

target_include_directories(udaf_ability_b_port
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_b_port
    INTERFACE
        udaf::udaf_core
        Threads::Threads
)

target_compile_features(udaf_ability_b_port INTERFACE cxx_std_20)

add_library(udaf::udaf_ability_b_port ALIAS udaf_ability_b_port)