# CompilerWarnings.cmake - 统一编译警告选项
# 依据 CLAUDE.md §"编译"：打开所有警告选项，新增代码不允许引入编译警告

# 默认开启的警告（GCC + Clang 通用）
set(UDAF_COMPILER_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
)

# GCC 专属
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND UDAF_COMPILER_WARNINGS
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
        -Wno-unknown-pragmas   # pragma GCC 跨编译器
    )
endif()

# Clang 专属
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    list(APPEND UDAF_COMPILER_WARNINGS
        -Wrange-loop-construct
        -Wgnu-zero-variadic-macro-arguments
        -Wc++20-compat
    )
endif()

# 覆盖率标志（GCC / Clang 通用）
set(UDAF_COVERAGE_FLAGS
    --coverage
    -fprofile-arcs
    -ftest-coverage
    -fprofile-update=atomic
)
set(UDAF_COVERAGE_LINK_FLAGS
    --coverage
)

# Sanitizer 标志（按选项启用）
if(UDAF_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(UDAF_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()
if(UDAF_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# 输出配置状态
message(STATUS "[CompilerWarnings.cmake] 警告等级: -Wall -Wextra -Wpedantic + Wshadow/Wconversion + sign-conversion")