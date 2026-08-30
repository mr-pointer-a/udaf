# tests/sdk/c_api 单元测试
add_executable(test_c_api test_c_api.cpp)
target_link_libraries(test_c_api
    PRIVATE
        udaf::udaf_c
        GTest::gtest_main)
target_compile_features(test_c_api PRIVATE cxx_std_20)
include(GoogleTest)
gtest_discover_tests(test_c_api PROPERTIES LABELS "unit" TIMEOUT 60)
