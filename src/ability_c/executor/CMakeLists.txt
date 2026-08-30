# src/ability_c/executor 模块 - 阶段 D4
# 详细设计见 docs/04-module-design.md §2.11 + docs/03-detailed-design.md §9.x
#
# 公开 API：
#   udaf::ability_c::executor::ProcessExecutor
#   - Options + Result 嵌套结构

add_library(udaf_ability_c_executor STATIC
    process_executor.cpp
)

target_include_directories(udaf_ability_c_executor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_c_executor
    PUBLIC
        udaf::udaf_core
)

target_compile_features(udaf_ability_c_executor PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_c_executor ALIAS udaf_ability_c_executor)