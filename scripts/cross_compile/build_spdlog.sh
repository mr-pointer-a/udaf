#!/usr/bin/env bash
# build_spdlog.sh - 交叉编译 spdlog 1.12（C++ 日志框架）
#
# 来源: https://github.com/gabime/spdlog
# 用途: UDAF 日志层（基于 fmt header-only）
# 依赖: fmt header-only（应先构建）
# 产物: ${INSTALL_PREFIX}/lib/libspdlog.a（可选；spdlog 默认 header-only）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"
log_info "spdlog ${LIB_VERSION} for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

SRC_DIR="${WORK_ROOT}/spdlog-${LIB_VERSION}"
ARCHIVE="spdlog-${LIB_VERSION}.tar.gz"
URL="https://github.com/gabime/spdlog/archive/refs/tags/v${LIB_VERSION}.tar.gz"

cd "${WORK_ROOT}"
if [[ ! -f "${ARCHIVE}" ]]; then
    log_info "下载 ${URL}"
    curl -fL --retry 3 -o "${ARCHIVE}" "${URL}"
fi

if [[ ! -d "${SRC_DIR}" ]]; then
    log_info "解压 ${ARCHIVE}"
    tar xzf "${ARCHIVE}"
fi

cd "${SRC_DIR}"

mkdir -p build-static
cd build-static

# spdlog 默认 header-only（无需构建 .a）；通过 SPDLOG_BUILD_LIB 控制
cmake .. \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
    -DCMAKE_AR="${AR_TOOL}" \
    -DCMAKE_RANLIB="${RANLIB_TOOL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPDLOG_FMT_EXTERNAL=ON \
    -DSPDLOG_INSTALL=ON \
    -DBUILD_SHARED_LIBS=OFF

cmake --build . -j"$(nproc)"
cmake --install .

log_success "spdlog 头文件已安装到 ${INSTALL_PREFIX}/include/spdlog/"
log_info "（spdlog 默认 header-only，无需 .a 链接）"