# tests/integration 跨模块集成测试
# §10.1 discovery→bridge→topology→node 主链路
# §10.2 心跳聚合
# §10.3 加密链路
# §10.4 跨主机白名单调度
# §10.5 节点生命周期 + 审计 + 可观测性
# §10.6 SDK Client 全链路

add_executable(test_integration test_integration.cpp)

target_link_libraries(test_integration
    PRIVATE
        udaf::udaf_core
        udaf::udaf_crypto
        udaf::udaf_audit
        udaf::udaf_observability
        udaf::udaf_sdk
        udaf::udaf_ability_a_registry
        udaf::udaf_ability_a_bridge
        udaf::udaf_ability_a_trust
        udaf::udaf_ability_b_topology
        udaf::udaf_ability_b_node
        udaf::udaf_ability_c_nodes
        udaf::udaf_ability_c_executor
        OpenSSL::Crypto
        GTest::gtest_main
)

target_compile_features(test_integration PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_integration
    PROPERTIES
        LABELS "integration"
        TIMEOUT 600
)