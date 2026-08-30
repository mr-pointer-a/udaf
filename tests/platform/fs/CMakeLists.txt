# tests/platform/fs 单元测试 - 阶段 A2
# 详细用例清单见 docs/05-test-plan.md §5.3.2

add_executable(test_platform_fs
    test_unique_fd.cpp          # UniqueFd RAII + EINTR（6 用例）
    test_wal.cpp                # Wal 全部功能（7 用例）
)

target_link_libraries(test_platform_fs
    PRIVATE
        udaf::udaf_platform_fs
        udaf::udaf_core
        GTest::gtest_main
)

target_compile_features(test_platform_fs PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_platform_fs
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)