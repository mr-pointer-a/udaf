# bench/ Google Benchmark 性能基准
# 详细清单见 docs/02-architecture.md §3.4 + docs/03-detailed-design.md §11.2 + docs/05-test-plan.md §5.5.1
# 共 29 项性能契约

add_executable(udaf_bench udaf_bench.cpp)

target_link_libraries(udaf_bench
    PRIVATE
        udaf::udaf_core
        udaf::udaf_audit
        udaf::udaf_crypto
        udaf::udaf_observability
        udaf::udaf_sdk
        udaf::udaf_ability_a_registry
        udaf::udaf_ability_b_transport
        udaf::udaf_ability_b_topology
        udaf::udaf_ability_b_port
        udaf::udaf_ability_b_node
        udaf::udaf_ability_c_nodes
        udaf::udaf_ability_c_executor
        benchmark::benchmark
)

target_compile_features(udaf_bench PRIVATE cxx_std_20)

# udaf_bench 是 Google Benchmark 而非 gtest，使用 add_test 而非 gtest_discover_tests
add_test(
    NAME udaf_bench_smoke
    COMMAND udaf_bench --benchmark_min_time=0.01s
)
set_tests_properties(udaf_bench_smoke PROPERTIES
    LABELS "benchmark"
    TIMEOUT 1800   # 30 分钟
    ENVIRONMENT "UDAF_BENCH_OUTPUT=build-release/bench_results.json"
)

# 也允许外部 benchmark 驱动（README 用）
add_custom_target(run-benchmarks
    COMMAND ${CMAKE_CTEST_COMMAND} -L benchmark --output-on-failure
    DEPENDS udaf_bench
    COMMENT "运行 29 项性能契约基准（阶段 C/E 实施时扩展）"
    USES_TERMINAL
)