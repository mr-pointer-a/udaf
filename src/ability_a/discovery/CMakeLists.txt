# src/ability_a/discovery 模块 - 阶段 D1
# 详细设计见 docs/04-module-design.md §2.9
#
# 公开 API：
#   udaf::ability_a::discovery::Advertiser (std::thread broadcast_thread_)
#   udaf::ability_a::discovery::Scanner    (shared_mutex)
#   udaf::ability_a::discovery::AdvertisementPayload (nonce + 32B MAC + 重放防护)

add_library(udaf_ability_a_discovery STATIC
    advertisement.cpp
    advertiser.cpp
    scanner.cpp
)

target_include_directories(udaf_ability_a_discovery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_a_discovery
    PUBLIC
        udaf::udaf_core
        udaf::udaf_crypto
        udaf::udaf_ability_a_transport
        udaf::udaf_ability_a_registry
        Threads::Threads
)

target_compile_features(udaf_ability_a_discovery PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_a_discovery ALIAS udaf_ability_a_discovery)