# src/core 模块 - 阶段 A1
# 详细设计见 docs/04-module-design.md §2.1 + docs/03-detailed-design.md §1.3
#
# 公开 API（阶段 A1 实现）：
#   udaf::core::Result<T>  /  Result<void>  (8 个方法，[[nodiscard]])
#   udaf::core::ErrorCode  (61 条 SCREAMING_SNAKE，移植自 ADR-011)
#   udaf::core::Logger     (spdlog 后端)
#   udaf::core::ConfigLoader (yaml-cpp 后端)
#   udaf::core::RingBuffer / DynamicRingBuffer (SPSC 零拷贝环形队列)

add_library(udaf_core STATIC
    error_code.cpp
    log/logger.cpp
    config/config_loader.cpp
    buffer/buffer.cpp
)

target_include_directories(udaf_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(udaf_core
    PUBLIC
        Threads::Threads
        spdlog::spdlog
)
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(udaf_core PUBLIC yaml-cpp::yaml-cpp)
endif()

target_compile_features(udaf_core PUBLIC cxx_std_20)

add_library(udaf::udaf_core ALIAS udaf_core)