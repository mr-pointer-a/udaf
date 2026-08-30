# tests/cli 单元测试 - 阶段 E4
add_executable(test_cli test_cli.cpp)

target_link_libraries(test_cli
    PRIVATE
        udaf_cli
        GTest::gtest_main
)

target_compile_features(test_cli PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_cli
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)
