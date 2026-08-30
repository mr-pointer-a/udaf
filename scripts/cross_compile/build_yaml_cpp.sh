#!/usr/bin/env bash
# build_yaml_cpp.sh - 交叉编译 yaml-cpp 0.8.x（YAML 配置解析）
#
# 来源: https://github.com/jbeder/yaml-cpp
# 用途: UDAF 拓扑 / 配置文件解析
# 依赖: 无
# 产物: ${INSTALL_PREFIX}/lib/libyaml-cpp.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib_install.sh
source "${SCRIPT_DIR}/../lib_install.sh"

: "${LIB_VERSION:?LIB_VERSION 未设置}"
: "${TARGET_ARCH:?TARGET_ARCH 未设置}"
: "${INSTALL_PREFIX:?INSTALL_PREFIX 未设置}"
: "${WORK_ROOT:?WORK_ROOT 未设置}"
log_info "yaml-cpp ${LIB_VERSION} for ${TARGET_ARCH} → ${INSTALL_PREFIX}"

SRC_DIR="${WORK_ROOT}/yaml-cpp-${LIB_VERSION}"
ARCHIVE="yaml-cpp-${LIB_VERSION}.tar.gz"
URL="https://github.com/jbeder/yaml-cpp/archive/refs/tags/${LIB_VERSION}.tar.gz"

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
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DYAML_CPP_BUILD_TESTS=OFF \
    -DYAML_CPP_BUILD_TOOLS=OFF \
    -DYAML_CPP_BUILD_CONTRIB=OFF \
    -DYAML_CPP_INSTALL=ON

# 4. 编译
cmake --build . -j"$(nproc)"

# 5. 安装
cmake --install .

log_success "yaml-cpp.a 已安装到 ${INSTALL_PREFIX}/lib/libyaml-cpp.a"