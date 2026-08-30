#!/usr/bin/env bash
# scripts/make_deb.sh - Debian 包打包入口
# 详见 cmake/PackageDeb.cmake
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_TYPE="${BUILD_TYPE:-Release}"
VERSION="${VERSION:-0.1.0}"
ARCH="${ARCH:-amd64}"

echo "[make_deb] BUILD_TYPE=${BUILD_TYPE} VERSION=${VERSION} ARCH=${ARCH}"

# 1) 配置 + 构建 Release
rm -rf "build-deb-${ARCH}"
cmake -B "build-deb-${ARCH}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DUDAF_VERSION="${VERSION}" \
    -DUDAF_DEB_ARCH="${ARCH}"
cmake --build "build-deb-${ARCH}" -j"$(nproc)"

# 2) CPack 打包
cd "build-deb-${ARCH}"
cpack -G DEB

# 3) 验证
DEB_FILE="udaf-${VERSION}-Linux.deb"
if [ -f "${DEB_FILE}" ]; then
    echo "[make_deb] 成功生成 ${DEB_FILE}"
    dpkg-deb -I "${DEB_FILE}"
    dpkg-deb -c "${DEB_FILE}"
else
    echo "[make_deb] 失败：未找到 ${DEB_FILE}" >&2
    exit 1
fi