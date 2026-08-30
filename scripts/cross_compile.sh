#!/usr/bin/env bash
# scripts/cross_compile.sh - aarch64 交叉编译入口
# 目标：linux-aarch64 设备端
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

BUILD_DIR="build-${ARCH}"
rm -rf "${BUILD_DIR}"

cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUDAF_CROSS_COMPILE=ON

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[cross] 交叉编译完成：${BUILD_DIR}/"
file "${BUILD_DIR}/src/cli/udaf" 2>/dev/null || true