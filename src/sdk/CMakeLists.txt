# src/sdk 模块入口（统一托管 sdk/ 子目录 + C 接口）
add_subdirectory(sdk)

add_library(udaf_c STATIC udaf_c.cpp)
target_include_directories(udaf_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(udaf_c PUBLIC
    udaf::udaf_sdk
    udaf::udaf_core)
target_compile_features(udaf_c PUBLIC cxx_std_20)
add_library(udaf::udaf_c ALIAS udaf_c)