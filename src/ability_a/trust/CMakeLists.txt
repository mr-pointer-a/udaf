# src/ability_a/trust 模块 - 阶段 D3
# 详细设计见 docs/04-module-design.md §2.2 + §3 (trust subdirectory)
#
# 公开 API：
#   udaf::ability_a::trust::PeerWhitelist
#   - 字段：fingerprint_sha256_ + allowed_capabilities_

add_library(udaf_ability_a_trust STATIC
    peer_whitelist.cpp
)

target_include_directories(udaf_ability_a_trust
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_a_trust
    PUBLIC
        udaf::udaf_core
        udaf::udaf_crypto
        Threads::Threads
)

target_compile_features(udaf_ability_a_trust PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_a_trust ALIAS udaf_ability_a_trust)