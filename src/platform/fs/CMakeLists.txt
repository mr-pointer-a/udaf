# src/platform/fs 模块 - 阶段 A2
# 详细设计见 docs/04-module-design.md §2.8 + docs/03-detailed-design.md §6.3.2
#
# 公开 API：
#   udaf::platform::fs::UniqueFd  (RAII + Rule of Five + TEMP_FAILURE_RETRY)
#   udaf::platform::fs::Wal       (PIMPL + append/replay/truncate + schema_version_)

add_library(udaf_platform_fs STATIC
    wal.cpp
)

target_include_directories(udaf_platform_fs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_platform_fs
    PUBLIC
        Threads::Threads
        udaf::udaf_core
)

target_compile_features(udaf_platform_fs PUBLIC cxx_std_20)

add_library(udaf::udaf_platform_fs ALIAS udaf_platform_fs)