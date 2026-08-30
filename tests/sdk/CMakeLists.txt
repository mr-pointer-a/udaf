# tests/sdk 单元测试 - 阶段 E3
add_executable(test_sdk test_sdk.cpp)

target_link_libraries(test_sdk
    PRIVATE
        udaf::udaf_sdk
        GTest::gtest_main
)

target_compile_features(test_sdk PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_sdk
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)