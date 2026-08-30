# StaticAnalyzers.cmake - 静态分析器配置
# 依据 docs/05-test-plan.md §5.11.3 CI YAML（static-analysis job）

# clang-tidy 检查列表
set(UDAF_CLANG_TIDY_CHECKS
    bugprone-*
    cert-*
    clang-analyzer-*
    cppcoreguidelines-*
    hicpp-*
    modernize-*
    performance-*
    portability-*
    readability-*
    -bugprone-easily-swappable-parameters
    -cppcoreguidelines-avoid-magic-numbers
    -cppcoreguidelines-pro-bounds-array-to-pointer-decay
    -cppcoreguidelines-pro-bounds-constant-array-index
    -cppcoreguidelines-pro-type-reinterpret-cast
    -hicpp-avoid-c-arrays
    -hicpp-no-array-decay
    -modernize-use-trailing-return-type
    -readability-magic-numbers
)

# 当 CMAKE_CXX_CLANG_TIDY 被设置时启用 clang-tidy
if(DEFINED CMAKE_CXX_CLANG_TIDY)
    message(STATUS "[StaticAnalyzers.cmake] 启用 clang-tidy: ${UDAF_CLANG_TIDY_CHECKS}")
endif()

# cppcheck 抑制列表（CI 单独调用，不嵌入 cmake）
set(UDAF_CPPCHECK_FLAGS
    --enable=warning,style,performance,portability
    --inline-suppr
    --suppress=missingIncludeSystem
    --suppress=unusedFunction
    --error-exitcode=2
    --xml
    --xml-version=2
    -j 4
)