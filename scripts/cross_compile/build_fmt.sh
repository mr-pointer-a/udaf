#!/usr/bin/env bash
# build_fmt.sh - 交叉编译 fmt 9.x（C++ 格式化库）
#
# 来源: https://github.com/fmtlib/fmt
# 用途: UDAF 日志格式化（spdlog 后端依赖）
# 依赖: 无
# 产物: ${INSTALL_PREFIX}/lib/libfmt.a（可选；fmt 9.1+ 默认 header-only）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"
log_info "fmt ${LIB_VERSION} for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

# fmt 9.1 默认 header-only（无 .a 文件），spdlog 通过 #include <fmt/...> 使用
# 若用户希望链接静态库，需设置 FMT_BUILD_AS_LIB=ON

SRC_DIR="${WORK_ROOT}/fmt-${LIB_VERSION}"
ARCHIVE="fmt-${LIB_VERSION}.tar.gz"
URL="https://github.com/fmtlib/fmt/archive/refs/tags/${LIB_VERSION}.tar.gz"

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

cmake .. \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
    -DCMAKE_AR="${AR_TOOL}" \
    -DCMAKE_RANLIB="${RANLIB_TOOL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFMT_DOC=OFF \
    -DFMT_TEST=OFF \
    -DFMT_INSTALL=ON \
    -DBUILD_SHARED_LIBS=OFF

cmake --build . -j"$(nproc)"
cmake --install .

log_success "fmt 头文件已安装到 ${INSTALL_PREFIX}/include/fmt/"
log_info "（fmt 9.x 默认 header-only，无需 .a 链接）"