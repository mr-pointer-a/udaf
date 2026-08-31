#!/usr/bin/env bash
# scripts/cross_compile.sh - aarch64 交叉编译入口
# 目标：linux-aarch64 设备端
#
# 用法：bash scripts/cross_compile.sh linux-aarch64
#
# 前置：
#   1. apt 安装 g++-aarch64-linux-gnu（基础工具链）
#   2. 第三方依赖（OpenSSL/spdlog/zmq/yaml-cpp/protobuf-lite）通过
#      scripts/cross_compile_deps.sh linux-aarch64 预编译到 sysroot，
#      或安装 'libssl-dev:arm64 libspdlog-dev:arm64 libzmq3-dev:arm64' 等。
#   3. 若 sysroot 缺失，构建会失败并提示如何补齐。

set -euo pipefail

cd "$(dirname "$0")/.."

ARCH="${ARCH:-aarch64}"
TOOLCHAIN_FILE="cmake/toolchains/linux-${ARCH}.cmake"

if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    echo "[cross] 缺少 toolchain: ${TOOLCHAIN_FILE}" >&2
    exit 1
fi

# 检查交叉编译器
if ! command -v "${ARCH}-linux-gnu-g++" >/dev/null 2>&1; then
    echo "[cross] 未安装 ${ARCH}-linux-gnu-g++，尝试 apt 安装..." >&2
    sudo apt-get install -y "g++-${ARCH}-linux-gnu" "gcc-${ARCH}-linux-gnu"
fi

# 检测 sysroot 完整性（基础库 vs 第三方库）
SYSROOT="/usr/${ARCH}-linux-gnu"
SYSROOT_HAS_OPENSSL=0
SYSROOT_HAS_SPDLOG=0
SYSROOT_HAS_ZMQ=0
[ -d "${SYSROOT}/include/openssl" ] && SYSROOT_HAS_OPENSSL=1
[ -d "${SYSROOT}/include/spdlog" ]  && SYSROOT_HAS_SPDLOG=1
[ -d "${SYSROOT}/include/zmq.h" ]    && SYSROOT_HAS_ZMQ=1
# 也检测自建 sysroot
if [ -n "${UDAF_CROSS_PREFIX:-}" ] && [ -d "${UDAF_CROSS_PREFIX}/include/openssl" ]; then
    SYSROOT_HAS_OPENSSL=1
fi

echo "[cross] 工具链: $($ARCH-linux-gnu-g++ --version | head -1)"
echo "[cross] sysroot: ${SYSROOT}"
echo "[cross] sysroot 第三方库检测:"
echo "       OpenSSL : $([ $SYSROOT_HAS_OPENSSL -eq 1 ] && echo OK || echo MISSING)"
echo "       spdlog  : $([ $SYSROOT_HAS_SPDLOG  -eq 1 ] && echo OK || echo MISSING)"
echo "       libzmq  : $([ $SYSROOT_HAS_ZMQ     -eq 1 ] && echo OK || echo MISSING)"

if [ $SYSROOT_HAS_OPENSSL -eq 0 ]; then
    echo "" >&2
    echo "[cross] 错误：aarch64 sysroot 缺 OpenSSL 头文件" >&2
    echo "[cross] 修复方法（任选其一）：" >&2
    echo "       1) apt 安装发行版包（需要启用 arm64 架构）：" >&2
    echo "          sudo dpkg --add-architecture arm64" >&2
    echo "          sudo apt-get update" >&2
    echo "          sudo apt-get install libssl-dev:arm64 libspdlog-dev:arm64 \\" >&2
    echo "                                  libzmq3-dev:arm64 libyaml-cpp-dev:arm64" >&2
    echo "       2) 通过交叉编译依赖脚本预编译（适用于 buildroot 类系统）：" >&2
    echo "          sudo scripts/cross_compile_deps.sh linux-aarch64 \\" >&2
    echo "                  /opt/udaf/cross/aarch64" >&2
    echo "          UDAF_CROSS_PREFIX=/opt/udaf/cross/aarch64 \\" >&2
    echo "              bash scripts/cross_compile.sh linux-aarch64" >&2
    exit 2
fi

BUILD_DIR="build-${ARCH}"
rm -rf "${BUILD_DIR}"

EXTRA_ARGS=()
if [ -n "${UDAF_CROSS_PREFIX:-}" ]; then
    EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=${UDAF_CROSS_PREFIX}")
fi

cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUDAF_CROSS_COMPILE=ON \
    "${EXTRA_ARGS[@]}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[cross] 交叉编译完成：${BUILD_DIR}/"
file "${BUILD_DIR}/src/cli/udaf" 2>/dev/null || true