#!/usr/bin/env bash
# install_deps.sh - 一行安装 UDAF 完整开发依赖（Ubuntu/Debian）
#
# 用法：sudo ./install_deps.sh [--minimal] [--runtime-only] [--no-cross] [--no-packaging]
#
# 选项：
#   --minimal      仅运行时依赖 + 编译器（最小集合）
#   --runtime-only 仅运行时依赖（用于部署到设备端）
#   --no-cross     跳过交叉编译工具链
#   --no-packaging 跳过 deb 打包工具

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib_install.sh"

print_header "UDAF 第三方依赖安装脚本"

# 默认模式：完整开发机
INSTALL_MODE="full"
USE_CROSS=true
USE_PACKAGING=true

while [[ $# -gt 0 ]]; do
    case "$1" in
    --minimal)      INSTALL_MODE="minimal"; shift ;;
    --runtime-only) INSTALL_MODE="runtime";  shift ;;
    --no-cross)     USE_CROSS=false; shift ;;
    --no-packaging) USE_PACKAGING=false; shift ;;
    -h|--help)
        sed -n '2,11p' "$0"
        exit 0
        ;;
    *)
        log_error "未知参数: $1"
        exit 2
        ;;
    esac
done

log_info "安装模式: ${INSTALL_MODE}"
log_info "交叉编译工具链: ${USE_CROSS}"
log_info "deb 打包工具: ${USE_PACKAGING}"

# ---------- 包清单 ----------

# 运行时依赖（必装）
RUNTIME_DEPS=(
    libzmq3-dev        # ZMQ ≥ 4.3
    libssl-dev         # OpenSSL ≥ 3.0
    libyaml-cpp-dev    # yaml-cpp ≥ 0.8.0
    libfmt-dev         # fmt ≥ 9.0
    libspdlog-dev      # spdlog ≥ 1.10
    libprotobuf-dev    # protobuf lite v3.x
)

# 编译时测试/基准工具
BUILD_DEPS=(
    libgtest-dev       # GoogleTest ≥ 1.14
    libgmock-dev       # Google Mock
    libbenchmark-dev   # Google Benchmark ≥ 1.8
    protobuf-compiler  # protoc v3.x
)

# 编译工具链
TOOLCHAIN_DEPS=(
    cmake              # ≥ 3.20
    g++                # GCC ≥ 13
    clang              # Clang ≥ 15
    ccache             # ≥ 4.0
)

# 静态分析
STATIC_ANALYSIS_DEPS=(
    clang-tidy
    cppcheck
)

# 覆盖率
COVERAGE_DEPS=(
    lcov
)

# 模糊测试
FUZZ_DEPS=(
    qemu-user-static
)

# 交叉编译工具链
CROSS_DEPS=(
    gcc-aarch64-linux-gnu
    gcc-arm-linux-gnueabihf
    gcc-riscv64-linux-gnu
)

# 打包工具
PACKAGING_DEPS=(
    dpkg-dev
    debhelper
    fakeroot
    lintian
)

# ---------- 执行安装 ----------

apt_update

case "${INSTALL_MODE}" in
minimal)
    install_pkgs "${RUNTIME_DEPS[@]}" "${TOOLCHAIN_DEPS[@]}"
    ;;
runtime)
    install_pkgs libzmq5 libssl3 libfmt9 libspdlog1.12 libyaml-cpp0.8 libprotobuf-dev
    ;;
full|*)
    # 运行时 + 编译时 + 工具链 + 静态分析 + 覆盖率
    install_pkgs \
        "${RUNTIME_DEPS[@]}" \
        "${BUILD_DEPS[@]}" \
        "${TOOLCHAIN_DEPS[@]}" \
        "${STATIC_ANALYSIS_DEPS[@]}" \
        "${COVERAGE_DEPS[@]}"

    # 可选：模糊测试
    if ask_yn "安装模糊测试工具 (libFuzzer + QEMU)?" y; then
        install_pkgs "${FUZZ_DEPS[@]}"
    fi

    # 可选：交叉编译工具链
    if [[ "${USE_CROSS}" == true ]]; then
        if ask_yn "安装交叉编译工具链 (aarch64/armv7/riscv64)?" y; then
            install_pkgs "${CROSS_DEPS[@]}"
        fi
    fi

    # 可选：deb 打包
    if [[ "${USE_PACKAGING}" == true ]]; then
        if ask_yn "安装 deb 打包工具 (dpkg-dev/debhelper/lintian)?" y; then
            install_pkgs "${PACKAGING_DEPS[@]}"
        fi
    fi
    ;;
esac

# ---------- 验证 ----------

print_header "安装验证"

verify_lib() {
    local lib_name="$1"
    local header="$2"
    if find /usr/include -name "$(basename "$header")" 2>/dev/null | grep -q "$header"; then
        log_success "$lib_name: OK ($header)"
    else
        log_warn "$lib_name: 头文件未找到 ($header)"
    fi
}

verify_lib "libzmq"        "zmq.h"
verify_lib "OpenSSL"       "openssl/ssl.h"
verify_lib "yaml-cpp"      "yaml-cpp/yaml.h"
verify_lib "fmt"           "fmt/core.h"
verify_lib "spdlog"        "spdlog/spdlog.h"
verify_lib "protobuf-lite" "google/protobuf/message_lite.h"

verify_tool() {
    local tool_name="$1"
    if command -v "${tool_name}" > /dev/null 2>&1; then
        local ver
        ver=$("${tool_name}" --version 2>&1 | head -1)
        log_success "${tool_name}: ${ver}"
    else
        log_warn "${tool_name}: 未安装"
    fi
}

verify_tool cmake
verify_tool g++
verify_tool clang
verify_tool clang-tidy
verify_tool cppcheck
verify_tool ccache
verify_tool lcov

print_header "完成"
log_success "UDAF 依赖安装完成"
log_info "如需交叉编译嵌入式设备端依赖（无 apt 的 buildroot/busybox 系统），请使用:"
log_info "  scripts/cross_compile_deps.sh <target_arch>"