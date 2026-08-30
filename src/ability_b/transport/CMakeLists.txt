# src/ability_b/transport 模块 - 阶段 C3
# 详细设计见 docs/04-module-design.md §2.5 + docs/03-detailed-design.md §3.3.5
#
# 公开 API：
#   udaf::ability_b::transport::ChannelBase (类型擦除)
#   udaf::ability_b::transport::Channel<T> (模板包装)
#   udaf::ability_b::transport::InprocChannel (SPSC 零拷贝)
#   4 个枚举：MessagePriority / RecvStatus / SendResult / TransportType

add_library(udaf_ability_b_transport STATIC
    inproc_channel.cpp
)

target_include_directories(udaf_ability_b_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_b_transport
    PUBLIC
        udaf::udaf_core
        Threads::Threads
)

target_compile_features(udaf_ability_b_transport PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_b_transport ALIAS udaf_ability_b_transport)