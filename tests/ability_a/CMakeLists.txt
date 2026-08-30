# tests/ability_a 单元测试 - 阶段 B/D
add_executable(test_ability_a test_ability_a.cpp)

target_link_libraries(test_ability_a
    PRIVATE
        udaf::udaf_ability_a_registry
        udaf::udaf_ability_a_discovery
        udaf::udaf_ability_a_transport
        udaf::udaf_ability_a_trust
        udaf::udaf_ability_a_bridge
        GTest::gtest_main
)

target_compile_features(test_ability_a PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_ability_a
    PROPERTIES
        LABELS "unit"
        TIMEOUT 120
)