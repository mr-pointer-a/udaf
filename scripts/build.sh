#!/usr/bin/env bash
# build.sh - UDAF 构建入口（Debug / Release / Coverage 三种模式）
#
# 用法：
#   scripts/build.sh debug       # 含 ASan/UBSan/覆盖率（开发机推荐）
#   scripts/build.sh release     # Release 优化（用于跑 29 项性能基准）
#   scripts/build.sh coverage    # Debug + gcov 覆盖率
#   scripts/build.sh fuzz        # Clang + libFuzzer
#   scripts/build.sh clean       # 清理 build 目录

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=lib_install.sh
source "${SCRIPT_DIR}/lib_install.sh"

cd "${PROJECT_ROOT}"

# ---------- ccache 加速 ----------
# 如果 ccache 已安装则启用
if command -v ccache > /dev/null 2>&1; then
    export CMAKE_C_COMPILER_LAUNCHER=ccache
    export CMAKE_CXX_COMPILER_LAUNCHER=ccache
    log_info "已启用 ccache 编译缓存"
fi

BUILD_TYPE="${1:-debug}"

case "${BUILD_TYPE}" in
debug)
    log_info "构建模式: Debug（含 ASan + UBSan）"
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUDAF_ENABLE_TESTS=ON \
        -DUDAF_ENABLE_BENCH=ON \
        -DUDAF_ENABLE_ASAN=ON \
        -DUDAF_ENABLE_UBSAN=ON \
        -DUDAF_ENABLE_TSAN=OFF \
        -DUDAF_ENABLE_COVERAGE=OFF \
        -DUDAF_ENABLE_FUZZ=OFF
    cmake --build build -j"$(nproc)"
    log_success "Debug 构建完成：build/"
    log_info "运行测试: ctest --test-dir build --output-on-failure"
    ;;

release)
    log_info "构建模式: Release"
    cmake -B build-release -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DUDAF_ENABLE_TESTS=OFF \
        -DUDAF_ENABLE_BENCH=ON \
        -DUDAF_ENABLE_ASAN=OFF \
        -DUDAF_ENABLE_UBSAN=OFF \
        -DUDAF_ENABLE_FUZZ=OFF
    cmake --build build-release -j"$(nproc)"
    log_success "Release 构建完成：build-release/"
    log_info "运行基准: ctest --test-dir build-release -L benchmark --output-on-failure"
    ;;

coverage)
    log_info "构建模式: Coverage（gcov + lcov）"
    cmake -B build-cov -S . \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUDAF_ENABLE_TESTS=ON \
        -DUDAF_ENABLE_BENCH=OFF \
        -DUDAF_ENABLE_COVERAGE=ON
    cmake --build build-cov -j"$(nproc)"
    ctest --test-dir build-cov --output-on-failure
    log_info "生成覆盖率报告..."
    mkdir -p build-cov/coverage
    cd build-cov
    lcov --capture --directory . --output-file coverage/coverage.info 2>&1 | tail -5
    lcov --remove coverage/coverage.info '/usr/*' 'tests/*' --output-file coverage/coverage.info
    genhtml coverage/coverage.info --output-directory coverage/html
    log_success "覆盖率报告：build-cov/coverage/html/index.html"
    ;;

fuzz)
    log_info "构建模式: Fuzz（Clang + libFuzzer）"
    if ! command -v clang > /dev/null 2>&1; then
        log_error "fuzz 模式需要 Clang"
        exit 1
    fi
    cmake -B build-fuzz -S . \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUDAF_ENABLE_TESTS=OFF \
        -DUDAF_ENABLE_BENCH=OFF \
        -DUDAF_ENABLE_FUZZ=ON
    cmake --build build-fuzz -j"$(nproc)"
    log_success "Fuzz 构建完成：build-fuzz/"
    log_info "运行 fuzz: ctest --test-dir build-fuzz -L fuzz"
    ;;

clean)
    log_info "清理 build 目录"
    rm -rf build build-release build-cov build-fuzz
    log_success "已清理"
    ;;

*)
    log_error "未知构建模式: ${BUILD_TYPE}"
    echo "用法: $0 {debug|release|coverage|fuzz|clean}" >&2
    exit 2
    ;;
esac