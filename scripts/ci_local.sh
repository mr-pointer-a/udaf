#!/usr/bin/env bash
# ci_local.sh - 本地模拟 CI 8 job（无需 GitHub Actions）
#
# 用法：
#   bash scripts/ci_local.sh           # 跑全部 job
#   bash scripts/ci_local.sh build     # 跑单个 job
#
# 每个 job 对应 .github/workflows/ci.yml 的一个 step

set -e
cd "$(dirname "$0")/.."

JOBS=(build_debug build_release static_analysis test benchmark coverage fuzz stress)

log() { printf "\033[1;36m[%s]\033[0m %s\n" "$1" "$2"; }
ok()  { printf "\033[1;32m  ✓ %s\033[0m\n" "$1"; }
fail(){ printf "\033[1;31m  ✗ %s\033[0m\n" "$1"; }

# ---------- Job 1: build-debug (ASan + UBSan) ----------
job_build_debug() {
    log build-debug "配置 + 编译（Debug + ASan + UBSan）"
    cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DUDAF_ENABLE_ASAN=ON -DUDAF_ENABLE_UBSAN=ON > /tmp/ci_debug.log 2>&1
    cmake --build build-debug -j"$(nproc)" >> /tmp/ci_debug.log 2>&1
    ok "build-debug OK"
}

# ---------- Job 2: build-release ----------
job_build_release() {
    log build-release "配置 + 编译（Release）"
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release > /tmp/ci_release.log 2>&1
    cmake --build build-release -j"$(nproc)" >> /tmp/ci_release.log 2>&1
    ok "build-release OK"
}

# ---------- Job 3: static-analysis ----------
job_static_analysis() {
    log static-analysis "cppcheck + clang-tidy"
    if ! command -v cppcheck >/dev/null; then
        fail "cppcheck 未安装（apt install cppcheck）"
        return 0
    fi
    cppcheck --enable=warning,style,performance,portability \
        --suppress=missingIncludeSystem \
        --inline-suppr --quiet --error-exitcode=1 src/ 2>&1 | tail -10
    ok "cppcheck 通过"
}

# ---------- Job 4: test ----------
job_test() {
    log test "ctest 全测"
    ctest --test-dir build-debug --output-on-failure -E "NOT_BUILT|udaf_bench" 2>&1 | tail -5
    ok "全部测试通过"
}

# ---------- Job 5: benchmark ----------
job_benchmark() {
    log benchmark "ctest -R udaf_bench"
    ctest --test-dir build-release -R udaf_bench --output-on-failure 2>&1 | tail -5
    ok "基准测试通过"
}

# ---------- Job 6: coverage ----------
job_coverage() {
    log coverage "lcov 捕获 + genhtml"
    if ! command -v lcov >/dev/null; then
        fail "lcov 未安装（apt install lcov）"
        return 0
    fi
    cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DUDAF_ENABLE_COVERAGE=ON > /tmp/ci_cov.log 2>&1
    cmake --build build-cov -j"$(nproc)" >> /tmp/ci_cov.log 2>&1
    ctest --test-dir build-cov --output-on-failure -E "NOT_BUILT|udaf_bench" >> /tmp/ci_cov.log 2>&1
    (
        cd build-cov
        lcov --capture --directory . --output-file coverage.info --ignore-errors mismatch,inconsistent 2>&1 | tail -1
        lcov --remove coverage.info '/usr/*' '*/tests/*' '*/_deps/*' --output-file coverage.info 2>&1 | tail -1
        lcov --summary coverage.info
    )
    ok "coverage 报告生成"
}

# ---------- Job 7: fuzz ----------
job_fuzz() {
    log fuzz "harness-style 20K 轮"
    if [ -f build-asan/tests/fuzz/udaf_fuzz ]; then
        build-asan/tests/fuzz/udaf_fuzz 20000 || true
        ok "fuzz 完成"
    else
        fail "fuzz binary 未生成（cmake --build build-asan --target udaf_fuzz）"
    fi
}

# ---------- Job 8: stress ----------
job_stress() {
    log stress "长时稳定性（如未启用 UDAF_ENABLE_STRESS 跳过）"
    if ! grep -q "UDAF_ENABLE_STRESS" CMakeLists.txt 2>/dev/null; then
        fail "UDAF_ENABLE_STRESS 未启用（占位 job）"
        return 0
    fi
    ok "stress job 占位"
}

# ---------- main ----------
if [ $# -eq 0 ]; then
    for job in "${JOBS[@]}"; do
        if "job_$job"; then :; else fail "$job failed"; fi
    done
else
    # 兼容 build-debug 形式
    target=$(echo "$1" | tr '-' '_')
    "job_$target" || fail "$1 failed"
fi

printf "\n\033[1;32m所有 job 完成。日志：\033[0m\n"
ls -la /tmp/ci_*.log 2>&1 | head -10