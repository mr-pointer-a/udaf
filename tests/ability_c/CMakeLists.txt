# tests/ability_c 单元测试 - 阶段 D
add_executable(test_ability_c test_ability_c.cpp)

target_link_libraries(test_ability_c
    PRIVATE
        udaf::udaf_ability_c_nodes
        udaf::udaf_ability_c_messages
        udaf::udaf_ability_c_executor
        udaf::udaf_ability_b_node
        udaf::udaf_ability_b_port
        udaf::udaf_ability_b_transport
        GTest::gtest_main
)

target_compile_features(test_ability_c PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_ability_c
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)