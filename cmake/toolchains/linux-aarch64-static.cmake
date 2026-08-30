# linux-aarch64-static.cmake
# aarch64（ARM64）嵌入式设备端交叉编译 toolchain
# 静态链接所有 UDAF 第三方依赖（libzmq / openssl / yaml-cpp / fmt / spdlog / protobuf-lite）
#
# 用法：
#   cmake -B build-device-aarch64 -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64-static.cmake \
#     -DCMAKE_PREFIX_PATH=/opt/udaf/cross/aarch64 \
#     -DCMAKE_BUILD_TYPE=Release

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉编译工具链前缀
set(CROSS_PREFIX "aarch64-linux-gnu")
set(CMAKE_C_COMPILER   "${CROSS_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}-g++")
set(CMAKE_AR           "${CROSS_PREFIX}-ar"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB       "${CROSS_PREFIX}-ranlib" CACHE FILEPATH "ranlib")
set(CMAKE_STRIP        "${CROSS_PREFIX}-strip"  CACHE FILEPATH "strip")
set(CMAKE_OBJCOPY      "${CROSS_PREFIX}-objcopy"CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${CROSS_PREFIX}-objdump"CACHE FILEPATH "objdump")

# 强制静态链接
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# sysroot 与依赖路径（来自 cross_compile_deps.sh 输出）
set(CMAKE_SYSROOT                       "/usr/${CROSS_PREFIX}"      CACHE PATH "")
set(CMAKE_FIND_ROOT_PATH                "${CMAKE_SYSROOT}"          CACHE PATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM  NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY  ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE  ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE  ONLY)

# 若 CMAKE_PREFIX_PATH 已设置（即 cross_compile_deps.sh 安装目录），优先
if(DEFINED CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_PREFIX_PATH}")
    # 第三方库的 pkg-config 路径
    set(PKG_CONFIG_PATH "${CMAKE_PREFIX_PATH}/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}")
endif()

# 编译选项
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion
    -fPIC
    -ffunction-sections -fdata-sections       # 减小可执行文件大小
    -fvisibility=hidden
    -fno-plt                                   # 减少 GOT 大小
)
add_link_options(
    -Wl,--gc-sections                          # 删除未使用段
    -Wl,-z,now                                 # 全量绑定 - 避免 dlopen
    -Wl,-z,relro                               # RELRO 安全加固
    -Wl,--as-needed                            # 仅链接需要的库
)

# 嵌入式设备端标准库与线程库
set(CMAKE_CXX_STANDARD_LIBRARY  "stdc++"        CACHE STRING "")
set(CMAKE_THREAD_LIBS_INIT      "pthread"       CACHE STRING "")
set(CMAKE_DL_LIBS_INIT          "dl"            CACHE STRING "")
set(CMAKE_USE_WIN32_THREADS_INIT OFF)
set(CMAKE_USE_PTHREADS_INIT      ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# 嵌入式无需测试/基准
set(UDAF_ENABLE_TESTS    OFF  CACHE BOOL "")
set(UDAF_ENABLE_BENCH    OFF  CACHE BOOL "")
set(UDAF_ENABLE_FUZZ     OFF  CACHE BOOL "")

message(STATUS "==========================================")
message(STATUS "UDAF 交叉编译 Toolchain: aarch64 静态")
message(STATUS "  C 编译器:    ${CMAKE_C_COMPILER}")
message(STATUS "  C++ 编译器:  ${CMAKE_CXX_COMPILER}")
message(STATUS "  Sysroot:     ${CMAKE_SYSROOT}")
message(STATUS "  Prefix:      ${CMAKE_PREFIX_PATH}")
message(STATUS "  Tests/Bench: OFF（嵌入式设备端）")
message(STATUS "==========================================")