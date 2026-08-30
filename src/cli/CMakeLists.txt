# src/cli 模块 - 阶段 E4
# 详细设计见 docs/03-detailed-design.md §9.7 + docs/adr/ADR-010-cli-conventions.md
#
# 公开 API：
#   udaf 命令行工具（14 个子命令 + cxxopts + Output 三态 + 13 退出码 + shell completion）
# 子命令：discover / run / push / pull / topology / node / trust / psk / auth
#         migrate / config / completion / version / help

add_executable(udaf main.cpp)

target_link_libraries(udaf
    PRIVATE
        udaf::udaf_sdk
        udaf::udaf_core
)

target_compile_features(udaf PRIVATE cxx_std_20)