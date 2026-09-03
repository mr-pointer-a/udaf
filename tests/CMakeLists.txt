# tests/ 顶层 CMakeLists.txt
# 详细组织见 docs/05-test-plan.md §5.3.0 - §5.10
#
# 测试分类：
#   - 单元测试（GTest）
#   - 集成测试（GTest + subprocess）
#   - 性能基准（Google Benchmark，29 项）
#   - 模糊测试（libFuzzer，Clang only）

# ---------- 单元测试子目录 ----------

add_subdirectory(core)              # 阶段 A
add_subdirectory(platform/fs)      # 阶段 A2
add_subdirectory(ability_a)       # 阶段 B/C/D
add_subdirectory(ability_b)       # 阶段 C
add_subdirectory(ability_c)       # 阶段 D
add_subdirectory(crypto)          # 阶段 B1
add_subdirectory(audit)           # 阶段 E1
add_subdirectory(observability)   # 阶段 E2
add_subdirectory(sdk)            # 阶段 E3
add_subdirectory(sdk/test_c_api) # C 接口
add_subdirectory(cli)            # 阶段 E4
add_subdirectory(fuzz)           # 模糊测试
add_subdirectory(fault_injection) # LD_PRELOAD 故障注入框架自检
add_subdirectory(integration)     # 跨模块集成测试
add_subdirectory(e2e)            # 端到端演示

# ---------- ctest 配置 ----------

include(CTest)

# 测试分类标签（用 ctest -L 过滤）
set(UDAF_TEST_LABEL_UNIT        "unit")
set(UDAF_TEST_LABEL_INTEGRATION "integration")
set(UDAF_TEST_LABEL_BENCHMARK   "benchmark")
set(UDAF_TEST_LABEL_FUZZ        "fuzz")
set(UDAF_TEST_LABEL_STRESS      "stress")

# ctest 默认超时（10 分钟）
set(CTEST_TIMEOUT 600 CACHE INTEGER "ctest 默认超时（秒）")