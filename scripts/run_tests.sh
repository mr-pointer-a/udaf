#!/usr/bin/env bash
# run_tests.sh - ctest 入口（按标签过滤）
#
# 用法：
#   scripts/run_tests.sh                  # 全部单元测试
#   scripts/run_tests.sh unit             # 单元测试（默认）
#   scripts/run_tests.sh integration      # 集成测试
#   scripts/run_tests.sh benchmark        # 性能基准（29 项）
#   scripts/run_tests.sh fuzz             # 模糊测试
#   scripts/run_tests.sh stress           # 压力测试
#   scripts/run_tests.sh all              # 全部
#   scripts/run_tests.sh --keep-going <label>  # 即使失败也继续

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=lib_install.sh
source "${SCRIPT_DIR}/lib_install.sh"

cd "${PROJECT_ROOT}"

# 默认 build 目录
BUILD_DIR="build"
KEEP_GOING=false

if [[ "${1:-}" == "--keep-going" ]]; then
    KEEP_GOING=true
    shift
fi

LABEL="${1:-unit}"

# 确定测试 build 目录（优先 Debug）
if [[ ! -d "${BUILD_DIR}" ]]; then
    if [[ -d "build-release" ]]; then
        BUILD_DIR="build-release"
    else
        log_error "未找到 build/ 目录，请先运行 scripts/build.sh"
        exit 1
    fi
fi

log_info "测试构建目录: ${BUILD_DIR}"
log_info "标签: ${LABEL}"

case "${LABEL}" in
unit)         CTEST_ARGS=(-L unit)        ;;
integration)  CTEST_ARGS=(-L integration) ;;
benchmark)    CTEST_ARGS=(-L benchmark)   ;;
fuzz)         CTEST_ARGS=(-L fuzz)        ;;
stress)       CTEST_ARGS=(-L stress)      ;;
all)          CTEST_ARGS=()               ;;
*)
    log_error "未知标签: ${LABEL}"
    exit 2
    ;;
esac

if [[ "${KEEP_GOING}" == true ]]; then
    CTEST_ARGS+=(--no-fail-fast)
fi

log_info "ctest ${CTEST_ARGS[*]} --output-on-failure"

ctest --test-dir "${BUILD_DIR}" "${CTEST_ARGS[@]}" --output-on-failure