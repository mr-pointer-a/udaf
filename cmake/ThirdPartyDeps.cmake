# ThirdPartyDeps.cmake - 第三方依赖查找
# 依据 docs/dependencies.md + docs/adr/ADR-009-dependency-management.md

# ---------- 运行时依赖 ----------

find_package(Threads REQUIRED)

# libzmq（≥ 4.3）
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZMQ libzmq>=4.3 IMPORTED_TARGET)
    if(NOT ZMQ_FOUND)
        message(WARNING "未找到 libzmq ≥ 4.3，将使用内置头文件 + 手动链接")
    endif()
endif()

# OpenSSL（≥ 3.0）
find_package(OpenSSL 3.0 REQUIRED)

# yaml-cpp（≥ 0.8）
find_package(yaml-cpp 0.8 QUIET)
if(NOT yaml-cpp_FOUND)
    message(WARNING "未找到 yaml-cpp ≥ 0.8，将使用内置头文件")
endif()

# fmt（≥ 9.0，header-only）
find_package(fmt 9.0 QUIET)
if(NOT fmt_FOUND)
    message(WARNING "未找到 fmt ≥ 9.0")
endif()

# spdlog（≥ 1.10）
find_package(spdlog 1.10 QUIET NO_DEFAULT_PATH PATHS /usr/lib/x86_64-linux-gnu/cmake)

# protobuf lite（≥ 3.21）
find_package(Protobuf 3.21 QUIET)

# ---------- 测试 / 基准依赖（仅开发机） ----------

if(UDAF_ENABLE_TESTS)
    find_path(_gtest_cmake_dir NAMES GTestConfig.cmake PATHS /usr/lib/x86_64-linux-gnu/cmake/GTest)
    if(_gtest_cmake_dir)
        get_filename_component(_gtest_cmake_dir_parent ${_gtest_cmake_dir} DIRECTORY)
        list(APPEND CMAKE_PREFIX_PATH "${_gtest_cmake_dir_parent}")
    endif()
    find_package(GTest 1.14 QUIET NO_DEFAULT_PATH PATHS /usr/lib/x86_64-linux-gnu/cmake)
    unset(_gtest_cmake_dir)
    unset(_gtest_cmake_dir_parent)
    if(NOT GTest_FOUND)
        message(STATUS "[ThirdPartyDeps] GTest 未通过 find_package 找到，将尝试本地编译")
    endif()
endif()

if(UDAF_ENABLE_BENCH)
    find_path(_benchmark_cmake_dir NAMES benchmarkConfig.cmake PATHS /usr/lib/x86_64-linux-gnu/cmake/benchmark)
    if(_benchmark_cmake_dir)
        get_filename_component(_benchmark_cmake_dir_parent ${_benchmark_cmake_dir} DIRECTORY)
        list(APPEND CMAKE_PREFIX_PATH "${_benchmark_cmake_dir_parent}")
    endif()
    find_package(benchmark 1.8 QUIET NO_DEFAULT_PATH PATHS /usr/lib/x86_64-linux-gnu/cmake)
    unset(_benchmark_cmake_dir)
    unset(_benchmark_cmake_dir_parent)
    if(NOT benchmark_FOUND)
        message(STATUS "[ThirdPartyDeps] Google Benchmark 未找到")
    endif()
endif()

# ---------- 导出依赖列表（供子模块使用） ----------

set(UDAF_PUBLIC_DEPS
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(yaml-cpp_FOUND)
    list(APPEND UDAF_PUBLIC_DEPS yaml-cpp::yaml-cpp)
endif()

if(fmt_FOUND)
    list(APPEND UDAF_PUBLIC_DEPS fmt::fmt)
endif()

if(spdlog_FOUND)
    list(APPEND UDAF_PUBLIC_DEPS spdlog::spdlog)
endif()

if(PkgConfig_FOUND AND ZMQ_FOUND)
    list(APPEND UDAF_PUBLIC_DEPS PkgConfig::ZMQ)
endif()

# 列出本项目用到的所有第三方依赖
set(UDAF_THIRD_PARTY_DEPS
    "libzmq (≥ 4.3)"
    "OpenSSL (≥ 3.0)"
    "yaml-cpp (≥ 0.8)"
    "fmt (≥ 9.0, header-only)"
    "spdlog (≥ 1.10)"
    "protobuf-lite (≥ 3.21)"
)

message(STATUS "[ThirdPartyDeps.cmake] 已链接运行时依赖:")
foreach(dep ${UDAF_THIRD_PARTY_DEPS})
    message(STATUS "  - ${dep}")
endforeach()