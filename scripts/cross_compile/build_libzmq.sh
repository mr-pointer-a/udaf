#!/usr/bin/env bash
# build_libzmq.sh - 交叉编译 libzmq（ZMQ 消息中间件）
#
# 来源: https://github.com/zeromq/libzmq
# 用途: UDAF 数据流层 transport 实现（inproc / ipc / tcp 三层传输）
# 依赖: 无（仅 libc + pthread）
# 产物: ${INSTALL_PREFIX}/lib/libzmq.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"

log_info "libzmq ${LIB_VERSION} for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

SRC_DIR="${WORK_ROOT}/libzmq-${LIB_VERSION}"
ARCHIVE="libzmq-${LIB_VERSION}.tar.gz"
URL="https://github.com/zeromq/libzmq/releases/download/v${LIB_VERSION}/${ARCHIVE}"

# 1. 下载
cd "${WORK_ROOT}"
if [[ ! -f "${ARCHIVE}" ]]; then
    log_info "下载 ${URL}"
    curl -fL --retry 3 -o "${ARCHIVE}" "${URL}"
fi

# 2. 解压
if [[ ! -d "${SRC_DIR}" ]]; then
    log_info "解压 ${ARCHIVE}"
    tar xzf "${ARCHIVE}"
fi

cd "${SRC_DIR}"

# 3. 配置（CMake）
mkdir -p build-static
cd build-static

cmake .. \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_C_COMPILER="${CC_COMPILER}" \
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
    -DCMAKE_AR="${AR_TOOL}" \
    -DCMAKE_RANLIB="${RANLIB_TOOL}" \
    -DCMAKE_STRIP="${STRIP_TOOL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_TESTS=OFF \
    -DENABLE_DRAFTS=OFF \
    -DWITH_PERF_TOOL=OFF \
    -DWITH_DOC=OFF \
    -DENABLE_CPACK=OFF

# 4. 编译
cmake --build . -j"$(nproc)"

# 5. 安装
cmake --install .

log_success "libzmq.a 已安装到 ${INSTALL_PREFIX}/lib/libzmq.a"