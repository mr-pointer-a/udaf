# src/cli 模块 - 阶段 E4
# 详细设计见 docs/03-detailed-design.md §9.7 + docs/adr/ADR-010-cli-conventions.md
#
# 公开 API：
#   udaf 命令行工具（14 个子命令 + cxxopts + Output 三态 + 13 退出码 + shell completion）
# 子命令：discover / run / push / pull / topology / node / trust / psk / auth
#         migrate / config / completion / version / help
#
# 拆分：
#   - udaf_cli 库：cli.cpp（14 个 handler + command_table + Client 单例）
#   - udaf 可执行文件：main.cpp（入口）链接 udaf_cli
#   - test_cli 直接链接 udaf_cli 调用 handler（无需 fork+exec）

add_library(udaf_cli STATIC cli.cpp)
target_include_directories(udaf_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(udaf_cli
    PUBLIC
        udaf::udaf_sdk
        udaf::udaf_core
)
target_compile_features(udaf_cli PUBLIC cxx_std_20)

add_executable(udaf main.cpp)
target_link_libraries(udaf
    PRIVATE
        udaf_cli
)
target_compile_features(udaf PRIVATE cxx_std_20)
