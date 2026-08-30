#!/usr/bin/env bash
# build_protobuf.sh - 交叉编译 protobuf 3.21.x（lite runtime + protoc 编译器）
#
# 来源: https://github.com/protocolbuffers/protobuf
# 用途: UDAF 节点消息契约序列化（ADR-002）
# 依赖: 无
# 产物:
#   ${INSTALL_PREFIX}/lib/libprotobuf-lite.a
#   ${INSTALL_PREFIX}/bin/protoc    (交叉编译目标端的 protoc；主机端 protoc 由 apt 提供)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"
log_info "protobuf ${LIB_VERSION} (lite runtime) for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

SRC_DIR="${WORK_ROOT}/protobuf-${LIB_VERSION}"
ARCHIVE="protobuf-${LIB_VERSION}.tar.gz"
URL="https://github.com/protocolbuffers/protobuf/releases/download/v${LIB_VERSION}/${ARCHIVE}"

cd "${WORK_ROOT}"
if [[ ! -f "${ARCHIVE}" ]]; then
    log_info "下载 ${URL}"
    curl -fL --retry 3 -o "${ARCHIVE}" "${URL}"
fi

if [[ ! -d "${SRC_DIR}" ]]; then
    log_info "解压 ${ARCHIVE}"
    tar xzf "${ARCHIVE}"
    cd "${SRC_DIR}"
    # 子模块初始化（lite runtime 不需要）
    # git submodule update -- --init --recursive
else
    cd "${SRC_DIR}"
fi

mkdir -p build-static
cd build-static

cmake ../cmake \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_C_COMPILER="${CC_COMPILER}" \
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" \
    -DCMAKE_AR="${AR_TOOL}" \
    -DCMAKE_RANLIB="${RANLIB_TOOL}" \
    -DCMAKE_STRIP="${STRIP_TOOL}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_EXAMPLES=OFF \
    -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
    -Dprotobuf_BUILD_LIBPROTOC=OFF \
    -Dprotobuf_WITH_ZLIB=OFF \
    -Dprotobuf_DISABLE_LITE=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON

cmake --build . -j"$(nproc)" --target libprotobuf-lite

cmake --install .

log_success "protobuf-lite 已安装:"
log_success "  libprotobuf-lite.a → ${INSTALL_PREFIX}/lib/libprotobuf-lite.a"
log_success "  头文件           → ${INSTALL_PREFIX}/include/google/protobuf/"
log_info "（protoc 编译器由 apt 主机端提供，不交叉编译）"