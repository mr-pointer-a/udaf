# src/ability_b/serialization 模块 - 阶段 C1
# 详细设计见 docs/04-module-design.md §2.7 + docs/03-detailed-design.md §3.3.7（评审 C-3 修复）
#
# 公开 API：
#   udaf::ability_b::serialization::SerializerBase (多态)
#   udaf::ability_b::serialization::Serializer<T, Derived> (CRTP 模板包装)

add_library(udaf_ability_b_serialization STATIC
    serializer.cpp
)

target_include_directories(udaf_ability_b_serialization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(udaf_ability_b_serialization
    PUBLIC
        udaf::udaf_core
)

target_compile_features(udaf_ability_b_serialization PUBLIC cxx_std_20)

add_library(udaf::udaf_ability_b_serialization ALIAS udaf_ability_b_serialization)