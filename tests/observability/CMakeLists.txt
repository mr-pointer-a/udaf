add_executable(test_observability test_observability.cpp)
target_link_libraries(test_observability PRIVATE
    udaf::udaf_observability GTest::gtest_main OpenSSL::Crypto)
target_compile_features(test_observability PRIVATE cxx_std_20)
include(GoogleTest)
gtest_discover_tests(test_observability PROPERTIES LABELS "unit" TIMEOUT 60)