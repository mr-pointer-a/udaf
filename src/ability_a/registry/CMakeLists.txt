# src/ability_a/registry 模块 - 阶段 B3
# 详细设计见 docs/04-module-design.md §2.2 + docs/03-detailed-design.md §9.4
#
# 公开 API：
#   udaf::ability_a::registry::ServiceRegistry (shared_mutex 多读单写)
#   udaf::ability_a::registry::SubscriptionHandle (RAII 句柄，评审 C-6)
#   udaf::ability_a::registry::RegistryEntry (字段 node_id_/hostname_/bind_address_/bind_port_/services_)

add_library(udaf_ability_a_registry STATIC
    service_registry.cpp
)

target_include_directories(udaf_ability_a_registry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_a_registry
    PUBLIC
        udaf::udaf_core
        Threads::Threads
)

target_compile_features(udaf_ability_a_registry PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_a_registry ALIAS udaf_ability_a_registry)