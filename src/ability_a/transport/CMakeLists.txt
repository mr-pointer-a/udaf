# src/ability_a/transport 模块 - 阶段 B2（UDP socket）
# 注意：与 ability_b::transport（ZMQ）严格区分（03 §1.3 命名空间表）
# 详细设计见 docs/04-module-design.md §2.9 + §2.2
#
# 公开 API：
#   udaf::ability_a::transport::UdpSocket  (RAII + 频率限制)

add_library(udaf_ability_a_transport STATIC
    udp_socket.cpp
)

target_include_directories(udaf_ability_a_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_a_transport
    PUBLIC
        udaf::udaf_core
        udaf::udaf_crypto
        udaf::udaf_platform_fs
)

target_compile_features(udaf_ability_a_transport PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_a_transport ALIAS udaf_ability_a_transport)