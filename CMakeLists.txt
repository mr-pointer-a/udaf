# UDAF 顶层 CMakeLists.txt
#
# 项目：Unified Device & Application Framework
# 设计依据：
#   - docs/01-requirements.md v1.0
#   - docs/02-architecture.md v2.8
#   - docs/03-detailed-design.md v2.1
#   - docs/04-module-design.md v0.7
#   - docs/05-test-plan.md v0.7
#
# 模块清单（11 核心模块，按依赖顺序）：
#   阶段 A：core + platform::fs
#   阶段 B：crypto + ability_a::transport + ability_a::registry + bridge
#   阶段 C：ability_b::{serialization, port, transport, topology, node}
#   阶段 D：ability_a::{discovery, bridge, trust} + ability_c::nodes
#   阶段 E：audit + observability + sdk + cli

cmake_minimum_required(VERSION 3.20)

project(udaf
    VERSION 0.1.0
    DESCRIPTION "Unified Device & Application Framework"
    HOMEPAGE_URL "https://github.com/udaf/udaf"
    LANGUAGES CXX C
)

# ---------- 工具链版本校验 ----------

set(UDAF_REQUIRED_GCC_MIN   "12.0")
set(UDAF_REQUIRED_CLANG_MIN "15.0")
set(UDAF_REQUIRED_CMAKE_MIN "3.20")

if(NOT CMAKE_VERSION VERSION_GREATER_EQUAL ${UDAF_REQUIRED_CMAKE_MIN})
    message(FATAL_ERROR "UDAF 需要 CMake ≥ ${UDAF_REQUIRED_CMAKE_MIN}，当前 ${CMAKE_VERSION}")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL ${UDAF_REQUIRED_GCC_MIN})
        message(WARNING "UDAF 推荐 GCC ≥ ${UDAF_REQUIRED_GCC_MIN}，当前 ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL ${UDAF_REQUIRED_CLANG_MIN})
        message(WARNING "UDAF 推荐 Clang ≥ ${UDAF_REQUIRED_CLANG_MIN}，当前 ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR
       CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "UDAF 不支持 ${CMAKE_CXX_COMPILER_ID}（仅 Linux GCC/Clang）")
endif()

# ---------- 全局选项 ----------

option(UDAF_ENABLE_TESTS   "启用单元测试 + 集成测试"      ON)
option(UDAF_ENABLE_BENCH   "启用 Google Benchmark 性能基准" ON)
option(UDAF_ENABLE_FUZZ    "启用 libFuzzer 模糊测试（需 Clang ≥ 15）" OFF)
option(UDAF_ENABLE_ASAN    "启用 AddressSanitizer（仅 Debug）" OFF)
option(UDAF_ENABLE_UBSAN   "启用 UndefinedBehaviorSanitizer（仅 Debug）" OFF)
option(UDAF_ENABLE_TSAN    "启用 ThreadSanitizer（仅 Debug）"  OFF)
option(UDAF_ENABLE_COVERAGE"启用覆盖率（仅 Debug + GCC/Clang）" OFF)
option(UDAF_ENABLE_PROPTEST "启用 RapidCheck 属性测试（自动发现）" OFF)

# 设备端构建时强制关闭 tests/bench/fuzz（已由 toolchain 设置）
if(UDAF_ENABLE_TESTS AND NOT DEFINED UDAF_TESTS_FORCED)
    # 正常主机端构建
endif()

# ---------- C++ 标准 ----------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- 共享 cmake 模块 ----------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(StaticAnalyzers)
include(PackageDeb)

# ---------- 编译警告 + 静态分析 ----------

add_compile_options(${UDAF_COMPILER_WARNINGS})
if(UDAF_ENABLE_COVERAGE)
    add_compile_options(${UDAF_COVERAGE_FLAGS})
    add_link_options(${UDAF_COVERAGE_LINK_FLAGS})
endif()

# ---------- 第三方依赖 ----------

include(ThirdPartyDeps)

# ---------- 公共编译定义 ----------

add_compile_definitions(
    UDAF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    UDAF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    UDAF_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ---------- 子目录（按依赖顺序） ----------

# 阶段 A：基础设施
add_subdirectory(src/core)
add_subdirectory(src/platform/fs)

# 阶段 B：能力 A 基础层
add_subdirectory(src/crypto)
add_subdirectory(src/bridge)
add_subdirectory(src/ability_a/transport)
add_subdirectory(src/ability_a/registry)

# 阶段 C：能力 B 数据流层
add_subdirectory(src/ability_b/serialization)
add_subdirectory(src/ability_b/port)
add_subdirectory(src/ability_b/transport)
add_subdirectory(src/ability_b/topology)
add_subdirectory(src/ability_b/node)

# 阶段 D：A 完整层 + 能力 C
add_subdirectory(src/ability_a/discovery)
add_subdirectory(src/ability_a/bridge)
add_subdirectory(src/ability_a/trust)
add_subdirectory(src/ability_c/executor)
add_subdirectory(src/ability_c/nodes)
add_subdirectory(src/ability_c/messages)

# 阶段 E：顶层 SDK + CLI
add_subdirectory(src/audit)
add_subdirectory(src/observability)
add_subdirectory(src/sdk)
add_subdirectory(src/cli)

# ---------- 测试 + 基准 ----------

if(UDAF_ENABLE_TESTS OR UDAF_ENABLE_PROPTEST)
    enable_testing()
endif()

if(UDAF_ENABLE_TESTS)
    add_subdirectory(tests)
endif()

if(UDAF_ENABLE_PROPTEST)
    add_subdirectory(tests/proptest)
endif()

if(UDAF_ENABLE_BENCH)
    add_subdirectory(bench)
endif()

# E2E 演示程序（always 构建，无 gtest）
add_executable(udaf_e2e tests/e2e/udaf_e2e.cpp)
target_link_libraries(udaf_e2e PRIVATE
    udaf::udaf_sdk
    udaf::udaf_audit
    udaf::udaf_ability_a_registry)
target_compile_features(udaf_e2e PRIVATE cxx_std_20)

# ---------- deb 打包（CPack） ----------

install(TARGETS udaf RUNTIME DESTINATION bin)
install(FILES README.md LICENSE CHANGELOG.md DESTINATION share/udaf)
install(DIRECTORY scripts/ DESTINATION share/udaf/scripts USE_SOURCE_PERMISSIONS)

include(CPack)

# ---------- 状态输出 ----------

message(STATUS "==========================================")
message(STATUS "UDAF v${PROJECT_VERSION} 配置完成")
message(STATUS "  C++ 编译器:    ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ 标准:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "  构建类型:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  测试:          ${UDAF_ENABLE_TESTS}")
message(STATUS "  基准:          ${UDAF_ENABLE_BENCH}")
message(STATUS "  模糊测试:      ${UDAF_ENABLE_FUZZ}")
message(STATUS "  ASan:          ${UDAF_ENABLE_ASAN}")
message(STATUS "  UBSan:         ${UDAF_ENABLE_UBSAN}")
message(STATUS "  TSan:          ${UDAF_ENABLE_TSAN}")
message(STATUS "  覆盖率:        ${UDAF_ENABLE_COVERAGE}")
message(STATUS "==========================================")