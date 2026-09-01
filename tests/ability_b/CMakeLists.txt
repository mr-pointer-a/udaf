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

# TcpChannel 单测（独立可执行，使用 ZMQ ROUTER fixture）
add_executable(test_tcp_channel test_tcp_channel.cpp)

target_link_libraries(test_tcp_channel
    PRIVATE
        udaf::udaf_ability_b_transport
        ${UDAF_ZMQ_LIB}
        GTest::gtest_main
)

target_include_directories(test_tcp_channel PRIVATE ${UDAF_ZMQ_INCLUDE_DIR})
target_compile_features(test_tcp_channel PRIVATE cxx_std_20)

gtest_discover_tests(test_tcp_channel
    PROPERTIES
        LABELS "unit"
        TIMEOUT 30
)