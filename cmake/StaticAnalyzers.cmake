# StaticAnalyzers.cmake - 静态分析器配置
# 依据 docs/05-test-plan.md §5.11.3 CI YAML（static-analysis job）

# clang-tidy 检查列表
# 排除项说明：
#   -bugprone-easily-swappable-parameters: 多参数接口常用，命名空间已避免歧义
#   -cppcoreguidelines-*: 项目允许魔数/数组索引等底层模式
#   -hicpp-*: 与 cppcoreguidelines 重叠
#   -modernize-use-trailing-return-type: 项目统一返回类型前置
#   -modernize-avoid-c-arrays: C 数组用于协议头/序列化字段
#   -modernize-raw-string-literal: 仅是风格偏好
#   -modernize-use-auto: 显式类型更易读
#   -modernize-make-unique: 部分场景需显式控制（异常安全）
#   -modernize-loop-convert: 仅是风格偏好
#   -performance-avoid-endl: \n 与 endl 性能差可忽略
#   -readability-magic-numbers: 测试与序列化常量多
#   -readability-identifier-length: 允许短变量名（i, n, p）
#   -readability-braces-around-statements: 单行 if/while 不强制大括号
#   -readability-isolate-declaration: 仅是风格偏好
#   -readability-implicit-bool-conversion: if(ptr)/if(size)是惯用 C++ 模式
#   -readability-static-definition-in-anonymous-namespace: 翻译单元内部 helper
#   -readability-named-parameter: tag dispatch 的合法未命名
#   -readability-redundant-declaration: 头文件显式声明更清晰
#   -bugprone-unhandled-exception-at-new: 项目禁止异常（CLAUDE.md §3.5）
#   -hicpp-braces-around-statements: 与 -readability-braces-around-statements 重复
#   -hicpp-signed-bitwise: 序列化字段宽度为字节切片，bitwise 在协议头广泛使用
#   -hicpp-use-auto: 项目偏好显式类型（与 -modernize-use-auto 一致）
#   -hicpp-named-parameter: tag dispatch 的合法未命名
#   -hicpp-explicit-conversions: 项目允许隐式算术转换（性能优化）
#   -cppcoreguidelines-pro-bounds-pointer-arithmetic: 序列化/网络协议层主动使用指针运算
#   -cppcoreguidelines-avoid-c-arrays: C 数组用于协议头/序列化字段
#   -cppcoreguidelines-pro-type-const-cast: 主动 const_cast 用于内部数据共享
#   -cppcoreguidelines-avoid-do-while: 流解析宏主动使用 do/while(0)
#   -cppcoreguidelines-avoid-non-const-global-variables: 进程级 once-init 全局状态
#   -cppcoreguidelines-avoid-goto / -hicpp-avoid-goto: OpenSSL EVP_* API 标准清理模式（goto done）
#   -cppcoreguidelines-pro-type-vararg / -hicpp-vararg: snprintf/fprintf 是 C API 必需
#   -cppcoreguidelines-special-member-functions / -hicpp-special-member-functions:
#       Rule of Five 由 -Wnon-virtual-dtor / 编译器 -Wdefaulted-function-deleted 单独强制
#   -clang-analyzer-optin.cplusplus.VirtualCall: 析构中显式调用虚函数用于清理（shutdown/close）
#   -readability-convert-member-functions-to-static: 实例无成员访问，保持 const + 实例语义
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
    -bugprone-unhandled-exception-at-new
    -clang-analyzer-optin.cplusplus.VirtualCall
    -cppcoreguidelines-avoid-c-arrays
    -cppcoreguidelines-avoid-do-while
    -cppcoreguidelines-avoid-goto
    -cppcoreguidelines-avoid-magic-numbers
    -cppcoreguidelines-avoid-non-const-global-variables
    -cppcoreguidelines-pro-bounds-array-to-pointer-decay
    -cppcoreguidelines-pro-bounds-constant-array-index
    -cppcoreguidelines-pro-bounds-pointer-arithmetic
    -cppcoreguidelines-pro-type-const-cast
    -cppcoreguidelines-pro-type-reinterpret-cast
    -cppcoreguidelines-pro-type-vararg
    -cppcoreguidelines-special-member-functions
    -hicpp-avoid-c-arrays
    -hicpp-avoid-goto
    -hicpp-braces-around-statements
    -hicpp-explicit-conversions
    -hicpp-named-parameter
    -hicpp-no-array-decay
    -hicpp-signed-bitwise
    -hicpp-special-member-functions
    -hicpp-use-auto
    -hicpp-vararg
    -modernize-avoid-c-arrays
    -modernize-loop-convert
    -modernize-make-unique
    -modernize-raw-string-literal
    -modernize-use-auto
    -modernize-use-trailing-return-type
    -performance-avoid-endl
    -readability-braces-around-statements
    -readability-convert-member-functions-to-static
    -readability-identifier-length
    -readability-isolate-declaration
    -readability-implicit-bool-conversion
    -readability-magic-numbers
    -readability-named-parameter
    -readability-redundant-declaration
    -readability-static-definition-in-anonymous-namespace
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
    --suppress=passedByValue
    --suppress=knownConditionTrueFalse
    --suppress=useStlAlgorithm
    --suppress=virtualCallInConstructor
    --suppress=unassignedVariable
    --error-exitcode=2
    --xml
    --xml-version=2
    -j 4
)