# src/sdk 模块 - 阶段 E3
add_library(udaf_sdk STATIC sdk.cpp)
target_include_directories(udaf_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(udaf_sdk PUBLIC
    udaf::udaf_core
    udaf::udaf_audit
    udaf::udaf_ability_a_registry
    udaf::udaf_ability_a_trust)
target_compile_features(udaf_sdk PUBLIC cxx_std_20)
add_library(udaf::udaf_sdk ALIAS udaf_sdk)