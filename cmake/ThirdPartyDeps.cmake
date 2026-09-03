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
# yaml-cpp cmake config 不设置 yaml_cpp_FOUND（它只创建 target），
# 用 target 存在性判断
if(NOT TARGET yaml-cpp::yaml-cpp)
    message(WARNING "未找到 yaml-cpp ≥ 0.8，将使用内置头文件")
endif()

# fmt（≥ 9.0，header-only）
find_package(fmt 9.0 QUIET)
if(NOT fmt_FOUND)
    message(WARNING "未找到 fmt ≥ 9.0")
endif()

# spdlog（≥ 1.10）
find_package(spdlog 1.10 QUIET)
if(NOT spdlog_FOUND)
    # 手动创建 imported target（fallback for CI multiarch）
    find_path(_spdlog_include_dir NAMES spdlog/spdlog.h PATHS /usr/include)
    find_library(_spdlog_lib NAMES spdlog PATHS /usr/lib/x86_64-linux-gnu)
    if(_spdlog_include_dir AND _spdlog_lib)
        add_library(spdlog::spdlog STATIC IMPORTED)
        set_target_properties(spdlog::spdlog PROPERTIES
            IMPORTED_LOCATION "${_spdlog_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_spdlog_include_dir}")
        set(spdlog_FOUND TRUE)
    endif()
    unset(_spdlog_include_dir)
    unset(_spdlog_lib)
endif()

# protobuf lite（≥ 3.21）
find_package(Protobuf 3.21 QUIET)

# ---------- 测试 / 基准依赖（仅开发机） ----------

if(UDAF_ENABLE_TESTS)
    find_package(GTest 1.14 QUIET)
    if(NOT GTest_FOUND)
        # fallback: 手动构建 imported target（STATIC 因为 CI 只有 .a 库）
        find_path(_gtest_include_dir NAMES gtest/gtest.h PATHS /usr/include)
        find_library(_gtest_lib NAMES gtest PATHS /usr/lib/x86_64-linux-gnu)
        find_library(_gtest_main_lib NAMES gtest_main PATHS /usr/lib/x86_64-linux-gnu)
        if(_gtest_include_dir AND _gtest_lib AND _gtest_main_lib)
            add_library(GTest::gtest STATIC IMPORTED)
            set_target_properties(GTest::gtest PROPERTIES
                IMPORTED_LOCATION "${_gtest_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${_gtest_include_dir}")
            add_library(GTest::gtest_main STATIC IMPORTED)
            set_target_properties(GTest::gtest_main PROPERTIES
                IMPORTED_LOCATION "${_gtest_main_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${_gtest_include_dir}")
            set(GTest_FOUND TRUE)
        endif()
        unset(_gtest_include_dir)
        unset(_gtest_lib)
        unset(_gtest_main_lib)
    endif()
endif()

if(UDAF_ENABLE_BENCH)
    find_package(benchmark 1.8 QUIET)
    if(NOT benchmark_FOUND)
        # fallback: 手动构建 imported target
        find_path(_benchmark_include_dir NAMES benchmark/benchmark.h PATHS /usr/include)
        find_library(_benchmark_lib NAMES benchmark PATHS /usr/lib/x86_64-linux-gnu)
        if(_benchmark_include_dir AND _benchmark_lib)
            add_library(benchmark::benchmark STATIC IMPORTED)
            set_target_properties(benchmark::benchmark PROPERTIES
                IMPORTED_LOCATION "${_benchmark_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${_benchmark_include_dir}")
            set(benchmark_FOUND TRUE)
        endif()
        unset(_benchmark_include_dir)
        unset(_benchmark_lib)
    endif()
endif()

# ---------- 导出依赖列表（供子模块使用） ----------

set(UDAF_PUBLIC_DEPS
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(TARGET yaml-cpp::yaml-cpp)
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