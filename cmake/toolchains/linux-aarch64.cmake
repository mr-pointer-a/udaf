# cmake/toolchains/linux-aarch64.cmake
# aarch64 设备端交叉编译 toolchain
#
# 用法：
#   cmake -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64.cmake \
#     [-DCMAKE_PREFIX_PATH=/opt/udaf/cross/aarch64]
#
# 若未提供 CMAKE_PREFIX_PATH，则只使用发行版提供的 /usr/aarch64-linux-gnu
# （仅含 libc/libstdc++ 等基础库；第三方依赖 OpenSSL/spdlog/zmq 等需
# 通过 scripts/cross_compile_deps.sh 预编译到 sysroot）。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# sysroot 搜索路径优先级：CMAKE_PREFIX_PATH（自建） > 发行版（仅基础库）
if(CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_PREFIX_PATH}")
endif()
list(APPEND CMAKE_FIND_ROOT_PATH "/usr/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 设备端构建时关闭 tests/bench/fuzz
set(UDAF_CROSS_COMPILE ON CACHE BOOL "设备端交叉编译" FORCE)

# 自检：sysroot 完整性
if(EXISTS "/usr/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/include/openssl")
    message(STATUS "[toolchain:aarch64] 发行版 sysroot 含 OpenSSL 头文件")
elseif(EXISTS "${CMAKE_PREFIX_PATH}/include/openssl")
    message(STATUS "[toolchain:aarch64] 自建 sysroot (${CMAKE_PREFIX_PATH}) 含 OpenSSL 头文件")
else()
    message(WARNING
        "[toolchain:aarch64] 未检测到 aarch64 OpenSSL 头文件。"
        "请通过 'sudo scripts/cross_compile_deps.sh aarch64' 构建 sysroot，"
        "或安装发行版包 'libssl-dev:arm64'。"
        "若只需验证 toolchain 语法，可跳过此警告。")
endif()