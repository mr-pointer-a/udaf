# src/ability_c/messages 模块 - 阶段 D4
# 公开 API：8 个业务消息结构体
add_library(udaf_ability_c_messages INTERFACE)
target_include_directories(udaf_ability_c_messages INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(udaf_ability_c_messages INTERFACE udaf::udaf_core)
target_compile_features(udaf_ability_c_messages INTERFACE cxx_std_20)
add_library(udaf::udaf_ability_c_messages ALIAS udaf_ability_c_messages)