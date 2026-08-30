# tests/ability_b 单元测试 - 阶段 C
add_executable(test_ability_b test_ability_b.cpp)

target_link_libraries(test_ability_b
    PRIVATE
        udaf::udaf_ability_b_serialization
        udaf::udaf_ability_b_port
        udaf::udaf_ability_b_transport
        udaf::udaf_ability_b_topology
        udaf::udaf_ability_b_node
        GTest::gtest_main
)

target_compile_features(test_ability_b PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_ability_b
    PROPERTIES
        LABELS "unit"
        TIMEOUT 120
)