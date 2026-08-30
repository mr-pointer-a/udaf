# src/observability 模块 - 阶段 E2
add_library(udaf_observability STATIC observability.cpp)
target_include_directories(udaf_observability PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(udaf_observability PUBLIC udaf::udaf_core OpenSSL::Crypto)
target_compile_features(udaf_observability PUBLIC cxx_std_20)
add_library(udaf::udaf_observability ALIAS udaf_observability)