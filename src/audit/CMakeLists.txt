# src/audit 模块 - 阶段 E1
# 详细设计见 docs/04-module-design.md §2.7 + ADR-006
#
# 公开 API：
#   udaf::audit::ActionType     (19 项枚举)
#   udaf::audit::AuditEvent
#   udaf::audit::AuditLogger    (SHA-512 hash chain + 文件 append)

add_library(udaf_audit STATIC
    audit.cpp
)

target_include_directories(udaf_audit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(udaf_audit
    PUBLIC
        udaf::udaf_core
        OpenSSL::Crypto
)

target_compile_features(udaf_audit PUBLIC cxx_std_20)

add_library(udaf::udaf_audit ALIAS udaf_audit)