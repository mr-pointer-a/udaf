# tests/crypto 单元测试 - 阶段 B1
add_executable(test_crypto test_crypto.cpp)

target_link_libraries(test_crypto
    PRIVATE
        udaf::udaf_crypto
        GTest::gtest_main
)

target_compile_features(test_crypto PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_crypto
    PROPERTIES
        LABELS "unit"
        TIMEOUT 120
)