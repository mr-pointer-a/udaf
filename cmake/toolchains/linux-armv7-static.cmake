# linux-armv7-static.cmake
# armv7（ARM 32-bit）嵌入式设备端交叉编译 toolchain
# 静态链接所有 UDAF 第三方依赖
#
# 用法：
#   cmake -B build-device-armv7 -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-armv7-static.cmake \
#     -DCMAKE_PREFIX_PATH=/opt/udaf/cross/armv7 \
#     -DCMAKE_BUILD_TYPE=Release

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ARMv7 硬浮点（cortex-a 系列）；若目标无 FPU 改为 arm-linux-gnueabi
set(CROSS_PREFIX "arm-linux-gnueabihf")
set(CMAKE_C_COMPILER   "${CROSS_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}-g++")
set(CMAKE_AR           "${CROSS_PREFIX}-ar"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB       "${CROSS_PREFIX}-ranlib" CACHE FILEPATH "ranlib")
set(CMAKE_STRIP        "${CROSS_PREFIX}-strip"  CACHE FILEPATH "strip")
set(CMAKE_OBJCOPY      "${CROSS_PREFIX}-objcopy"CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${CROSS_PREFIX}-objdump"CACHE FILEPATH "objdump")

# 强制静态链接
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# sysroot 与依赖路径
set(CMAKE_SYSROOT                       "/usr/${CROSS_PREFIX}"      CACHE PATH "")
set(CMAKE_FIND_ROOT_PATH                "${CMAKE_SYSROOT}"          CACHE PATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM  NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY  ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE  ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE  ONLY)

if(DEFINED CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_PREFIX_PATH}")
    set(PKG_CONFIG_PATH "${CMAKE_PREFIX_PATH}/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}")
endif()

# ARMv7 特定优化（ARMv7-A + Thumb-2）
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion
    -fPIC
    -ffunction-sections -fdata-sections
    -fvisibility=hidden
    -fno-plt
    -march=armv7-a
    -mfloat-abi=hard
    -mfpu=vfpv3-d16
    -mthumb
)
add_link_options(
    -Wl,--gc-sections
    -Wl,-z,now
    -Wl,-z,relro
    -Wl,--as-needed
    -Wl,--no-undefined
)

set(CMAKE_CXX_STANDARD_LIBRARY  "stdc++"        CACHE STRING "")
set(CMAKE_THREAD_LIBS_INIT      "pthread"       CACHE STRING "")
set(CMAKE_DL_LIBS_INIT          "dl"            CACHE STRING "")
set(CMAKE_USE_WIN32_THREADS_INIT OFF)
set(CMAKE_USE_PTHREADS_INIT      ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(UDAF_ENABLE_TESTS    OFF  CACHE BOOL "")
set(UDAF_ENABLE_BENCH    OFF  CACHE BOOL "")
set(UDAF_ENABLE_FUZZ     OFF  CACHE BOOL "")

message(STATUS "==========================================")
message(STATUS "UDAF 交叉编译 Toolchain: armv7 静态")
message(STATUS "  C 编译器:    ${CMAKE_C_COMPILER}")
message(STATUS "  C++ 编译器:  ${CMAKE_CXX_COMPILER}")
message(STATUS "  Sysroot:     ${CMAKE_SYSROOT}")
message(STATUS "  Prefix:      ${CMAKE_PREFIX_PATH}")
message(STATUS "  Tests/Bench: OFF（嵌入式设备端）")
message(STATUS "==========================================")