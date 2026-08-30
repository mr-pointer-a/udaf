#!/usr/bin/env bash
# cross_compile_deps.sh - 交叉编译 UDAF 运行时依赖（适用于 buildroot/busybox 等无 apt 系统）
#
# 用法：
#   scripts/cross_compile_deps.sh <target_arch> [<install_prefix>]
#
# 参数：
#   target_arch:      aarch64 | armv7 | riscv64
#   install_prefix:   默认 /opt/udaf/cross/<target_arch>
#
# 输出：
#   ${install_prefix}/lib/*.a   — 静态库（每个库一个）
#   ${install_prefix}/include/   — 头文件
#   ${install_prefix}/share/     — pkg-config 文件（可选）
#
# 之后构建 UDAF：
#   cmake -B build -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-<arch>-static.cmake \
#     -DCMAKE_PREFIX_PATH=<install_prefix>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CROSS_C_DIR="${SCRIPT_DIR}/cross_compile"

# 公共库
# shellcheck source=lib_install.sh
source "${SCRIPT_DIR}/lib_install.sh"

# ---------- 参数校验 ----------

if [[ $# -lt 1 ]]; then
    echo "用法: $0 <target_arch> [<install_prefix>]" >&2
    echo "  target_arch: aarch64 | armv7 | riscv64" >&2
    exit 2
fi

TARGET_ARCH="$1"
INSTALL_PREFIX="${2:-/opt/udaf/cross/${TARGET_ARCH}}"

case "${TARGET_ARCH}" in
    aarch64)   TOOLCHAIN_TRIPLE="aarch64-linux-gnu"      ;;
    armv7)     TOOLCHAIN_TRIPLE="arm-linux-gnueabihf"    ;;
    riscv64)   TOOLCHAIN_TRIPLE="riscv64-linux-gnu"      ;;
    *)
        log_error "不支持的目标架构: ${TARGET_ARCH}"
        log_error "支持: aarch64 / armv7 / riscv64"
        exit 2
        ;;
esac

CXX_COMPILER="${TOOLCHAIN_TRIPLE}-g++"
CC_COMPILER="${TOOLCHAIN_TRIPLE}-gcc"
AR_TOOL="${TOOLCHAIN_TRIPLE}-ar"
RANLIB_TOOL="${TOOLCHAIN_TRIPLE}-ranlib"
STRIP_TOOL="${TOOLCHAIN_TRIPLE}-strip"

# ---------- 前置检查 ----------

print_header "交叉编译环境检查"

for tool in "${CXX_COMPILER}" "${CC_COMPILER}" "${AR_TOOL}" "${RANLIB_TOOL}" "${STRIP_TOOL}"; do
    if ! command -v "${tool}" > /dev/null 2>&1; then
        log_error "工具链未找到: ${tool}"
        log_info "请先安装交叉编译工具链:"
        log_info "  Ubuntu/Debian: sudo apt-get install gcc-${TOOLCHAIN_TRIPLE}"
        log_info "  buildroot:    make ${TOOLCHAIN_TRIPLE}_defconfig"
        log_info "  Yocto:        bitbake meta-toolchain"
        exit 1
    fi
done

log_success "工具链就绪: ${TOOLCHAIN_TRIPLE}"
log_info "安装前缀: ${INSTALL_PREFIX}"
log_info "C++ 编译器: $(command -v ${CXX_COMPILER})"

# 创建安装目录
mkdir -p "${INSTALL_PREFIX}/lib" \
         "${INSTALL_PREFIX}/include" \
         "${INSTALL_PREFIX}/share" \
         "/tmp/udaf-cross-build"

# 工作目录（每个库一个子目录）
WORK_ROOT="/tmp/udaf-cross-build/${TARGET_ARCH}"
mkdir -p "${WORK_ROOT}"

# ---------- 库版本常量 ----------

LIBZMQ_VERSION="${LIBZMQ_VERSION:-4.3.5}"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.0.13}"
YAMLCPP_VERSION="${YAMLCPP_VERSION:-0.8.0}"
FMT_VERSION="${FMT_VERSION:-9.1.0}"
SPDLOG_VERSION="${SPDLOG_VERSION:-1.12.0}"
PROTOBUF_VERSION="${PROTOBUF_VERSION:-3.21.12}"

# ---------- 调度 ----------

print_header "UDAF 运行时依赖交叉编译（目标: ${TARGET_ARCH}）"

# 仅运行时依赖（编译时工具如 gtest/benchmark/protoc 在主机端用主机工具链处理，不交叉编译）
# 参考 docs/dependencies.md §6.3

LIBS_TO_BUILD=(
    "libzmq|${LIBZMQ_VERSION}"
    "openssl|${OPENSSL_VERSION}"
    "yaml-cpp|${YAMLCPP_VERSION}"
    "fmt|${FMT_VERSION}"
    "spdlog|${SPDLOG_VERSION}"
    "protobuf|${PROTOBUF_VERSION}"
)

START_TIME=$(date +%s)

for entry in "${LIBS_TO_BUILD[@]}"; do
    lib_name="${entry%%|*}"
    lib_ver="${entry##*|}"

    script_path="${CROSS_C_DIR}/build_${lib_name//-/_}.sh"

    if [[ ! -f "${script_path}" ]]; then
        log_warn "跳过: ${lib_name}（构建脚本不存在: ${script_path}）"
        continue
    fi

    log_info "=========================================="
    log_info "构建: ${lib_name} ${lib_ver}"
    log_info "=========================================="

    export TARGET_ARCH
    export TOOLCHAIN_TRIPLE
    export CXX_COMPILER CC_COMPILER AR_TOOL RANLIB_TOOL STRIP_TOOL
    export INSTALL_PREFIX WORK_ROOT
    export LIB_VERSION="${lib_ver}"

    bash "${script_path}"

    log_success "${lib_name} 构建完成"
done

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

# ---------- 输出报告 ----------

print_header "交叉编译完成报告"

log_info "目标架构:    ${TARGET_ARCH}"
log_info "工具链前缀:  ${TOOLCHAIN_TRIPLE}"
log_info "安装前缀:    ${INSTALL_PREFIX}"
log_info "耗时:        ${ELAPSED} 秒"

echo ""
echo "已构建静态库:"
if ls "${INSTALL_PREFIX}/lib/"*.a 2>/dev/null | head -10; then
    echo ""
    log_info "总大小: $(du -sh ${INSTALL_PREFIX}/lib/*.a 2>/dev/null | tail -1)"
else
    log_warn "未发现静态库输出"
fi

echo ""
echo "下一步：使用该交叉编译结果构建 UDAF"
echo "  cmake -B build-device-${TARGET_ARCH} -S . \\"
echo "    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-${TARGET_ARCH}-static.cmake \\"
echo "    -DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"