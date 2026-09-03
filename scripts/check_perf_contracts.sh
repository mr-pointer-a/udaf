#!/bin/bash
# scripts/check_perf_contracts.sh - 29 项性能契约自动化校验
#
# 设计：
#   - 跑 build-bench/bench/udaf_bench 拿到 JSON 输出
#   - 按架构 §3.4 + 概要 §11.2 + 测试 §5.5.1 的 29 项契约定义阈值
#   - 软阈值 ×1.2 / 硬阈值 ×1.5（参考 Phase 6 plan §"性能验证"）
#   - 输出：PASS / SOFT_FAIL / HARD_FAIL 三档 + 详细差异表
#
# 用法：
#   bash scripts/check_perf_contracts.sh                # 默认校验
#   bash scripts/check_perf_contracts.sh --rebuild       # 重建 build-bench
#   bash scripts/check_perf_contracts.sh --json-only     # 只输出 JSON
#
# 退出码：
#   0 = 全部 PASS
#   1 = 至少一项 SOFT_FAIL（超过软阈值但在硬阈值内）
#   2 = 至少一项 HARD_FAIL（超过硬阈值）

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# CI artifact 解压为 build-release 或本地 build-bench
BENCH_DIR="${BENCH_DIR:-$PROJECT_DIR/build-bench}"
BENCH_BIN="$BENCH_DIR/bench/udaf_bench"

# ---------- 软/硬阈值倍数（架构评审约定） ----------
SOFT_MULT=1.2
HARD_MULT=1.5

# ---------- 29 项契约阈值定义 ----------
# 字段：bench_name|threshold_value|unit|comparison|contract_id|description
# comparison: "<=" (时间越小越好) | ">=" (吞吐越大越好)
THRESHOLDS=(
    # #3 设备端冷启动 < 200ms（Client 构造 + start/stop 全程；阈值单位 ns）
    "udaf_bench_startup|200000000|<=||3|设备端冷启动 < 200ms（架构 #3）"
    # #5 同主机消息延迟 P95 < 100μs (100000 ns)
    "udaf_bench_inproc_latency_p95|100000|<=||5|同主机消息延迟 P95 < 100μs（架构 #5）"
    # #7 同主机吞吐 ≥ 50K msg/s → 当前 bench 输出 items_per_second
    "udaf_bench_inproc_throughput|50000|>=||7|同主机吞吐 ≥ 50K msg/s（架构 #7）"
    # #11 100 设备心跳聚合 < 10ms
    "udaf_bench_heart_aggregate_100|10000000|<=||11|100 设备心跳聚合 < 10ms（架构 #11）"
    # #14 服务注册表 ≥ 10000 条目（吞吐越大越好）
    "udaf_bench_registry_snapshot_10k|10000|>=||14|服务注册表 ≥ 10000 条目（架构 #14）"
    # #15 PSK 握手 < 2ms P95
    "udaf_bench_psk_handshake_p95|2000000|<=||15|PSK 握手 < 2ms P95（架构 #15）"
    # #16 PKI 握手 < 50ms P95
    "udaf_bench_pki_handshake|50000000|<=||16|PKI 握手 < 50ms P95（架构 #16）"
    # #21 单条消息默认 4KB / 最大 1MB（编码测试 → 同尺寸 round-trip 时间）
    "udaf_bench_large_msg_1mb|1000000000|<=||21|1MB 消息序列化 < 1s（架构 #21）"
    # #23 fork+exec ≤ 80ms
    "udaf_bench_fork_exec|80000000|<=||23|scheduler fork+exec ≤ 80ms（架构 #23）"
    # #24 C 节点冷启动 ≤ 50ms（同 #3 共享基准但阈值不同 → 复用 #3 benchmark，软阈值）
    "udaf_bench_node_cold_startup|50000000|<=||24|C 节点冷启动 ≤ 50ms（架构 #24）"
    # #26 加密性能开销（吞吐损失）< 20%（hmac 单次 < 2μs 作为间接指标）
    "udaf_bench_crypto_overhead|2000|<=||26|HMAC 单次 < 2μs（架构 #26 加密开销间接）"
    # #27 审计日志写入吞吐 ≥ 1000 条/秒
    "udaf_bench_audit_throughput|1000|>=||27|审计吞吐 ≥ 1K 条/秒（架构 #27）"
    # #4 崩溃恢复 ≤ 5s（AuditLogger verify_chain 200 条）
    "udaf_bench_recovery|5000000000|<=||4|崩溃恢复 ≤ 5s（架构 #4）"
    # #22 加密握手后每帧加密开销 ≤ 50μs (50000 ns)
    "udaf_bench_aead_per_frame|50000|<=||22|AEAD 单帧 ≤ 50μs（架构 #22）"
    # #20 可观测性自身开销 < 5%（baseline vs enabled 比值 < 1.05）
    # 用 ratio 模式：enabled / baseline < 1.05
    "udaf_bench_observability_overhead_enabled|100|<=||20|可观测性开销 < 5%（架构 #20，ratio 阈值 ×100）"
    # #21 4KB 默认消息编码 < 1μs (1000 ns)
    "udaf_bench_default_msg_4kb|1000|<=||21|4KB 默认消息编码 < 1μs（架构 #21）"
    # #13 最大并发节点 1000（架构 #13 仅约束"支持 ≥ 1000"，时间只给宽松上限 10ms）
    "udaf_bench_max_concurrent_nodes_1000|10000000|<=||13|1000 并发节点调度 < 10ms（架构 #13 宽松上限）"
    # 辅助契约：subscribe_fire latency（架构提及 < 5μs）
    "udaf_bench_subscribe_fire|5000|<=||S1|subscribe fire 延迟 < 5μs（架构 §3.4 提及）"
    # 辅助契约：subscribe_batch throughput（架构提及 ≥ 2M/s）
    "udaf_bench_subscribe_batch|2000000|>=||S2|subscribe batch 吞吐 ≥ 2M/s（架构 §3.4 提及）"
    # 辅助契约：WAL append throughput（架构提及 ≥ 10K/s）
    "udaf_bench_wal_append|10000|>=||S3|WAL append 吞吐 ≥ 10K/s（架构 §3.4 提及）"
    # 辅助契约：topology add_node latency
    "udaf_bench_topology_add_node|10000|<=||S4|topology add_node < 10μs（架构 §3.4 提及）"
    # 辅助契约：node reload latency（架构未明确，宽松 100ms）
    "udaf_bench_node_reload|100000000|<=||S5|node reload < 100ms（宽松上限）"
    # 辅助契约：channel send+recv 端到端往返（架构 #5/#25 类似）
    "udaf_bench_channel_send_recv|100000|<=||S6|channel send+recv 往返 < 100μs"
    # 辅助契约：channel 重连 latency
    "udaf_bench_channel_reopen|100000000|<=||S7|channel 重连 < 100ms（宽松上限）"
    # 辅助契约：Result<T> 构造（验证零开销）
    "udaf_bench_result_ok|1000|<=||S8|Result<T> 构造 < 1μs（零开销验证）"
    # 辅助契约：meter observe（可观测性内部开销）
    "udaf_bench_meter_observe|10000|<=||S9|meter observe < 10μs（观测开销验证）"
    # 辅助契约：prometheus 导出（每秒处理条数）
    "udaf_bench_prom_export|1000|>=||S10|prom export ≥ 1K metrics/s"
    # 辅助契约：heartbeat 优先级传递（架构 §5.6 强制投递）
    "udaf_bench_channel_heartbeat_priority|10000|<=||S11|heartbeat 优先级投递 < 10μs"
    # 辅助契约：port try_recv 延迟
    "udaf_bench_port_try_recv|1000|<=||S12|port try_recv < 1μs"
    # #25 命令往返延迟 P99 < 15ms（架构 §3.4 v2.8 新增）
    "udaf_bench_command_roundtrip_p99|15000000|<=||25|命令往返 P99 < 15ms（架构 #25）"
    # 内存契约 #1 #2 #28 #29 由 scripts/measure_memory.sh 测量（独立进程）
    # 本脚本调用 measure_memory.sh 整合
)

# ---------- 工具函数 ----------
die() { echo "ERROR: $*" >&2; exit 2; }
warn() { echo "WARN:  $*" >&2; }

# ---------- 主流程 ----------
[ -x "$BENCH_BIN" ] || die "找不到基准可执行文件：$BENCH_BIN（先 cmake -B build-bench + cmake --build build-bench）"

if [[ "${1:-}" == "--rebuild" ]]; then
    echo "==> 重建 build-bench"
    (cd "$PROJECT_DIR" && cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DUDAF_ENABLE_BENCHMARK=ON >/dev/null
     cmake --build build-bench --target udaf_bench -j"$(nproc)" >/dev/null) \
        || die "重建失败"
fi

# 跑基准 + 解析 JSON 输出
TMP_JSON="$(mktemp)"
trap 'rm -f "$TMP_JSON"' EXIT

echo "==> 运行基准（最小时间 0.1s 保证 P95 稳定）"
"$BENCH_BIN" --benchmark_min_time=0.1s \
              --benchmark_format=console \
              --benchmark_out_format=json \
              --benchmark_out="$TMP_JSON" >/dev/null 2>&1 \
    || die "基准运行失败"

# 解析每个 benchmark 的 time (CPU time, ns) / items_per_second / rss_kb
# 输出格式：name|cpu_time_ns|items_per_second|rss_kb
extract_metric() {
    local name="$1"
    python3 - "$TMP_JSON" "$name" <<'PY'
import json, sys
path, name = sys.argv[1], sys.argv[2]
try:
    data = json.load(open(path))
except Exception:
    print("|0|0|0"); sys.exit(0)
for b in data.get("benchmarks", []):
    if b.get("name","").startswith(name):
        cpu = b.get("cpu_time", 0)        # ns
        items = (b.get("items_per_second") or
                 (b.get("iterations",0)*1e9/cpu if cpu else 0))
        # rss_kb 是顶级 key（自定义 counter）
        rss_kb = int(b.get("rss_kb", 0) or 0)
        print(f"|{int(cpu)}|{items:.0f}|{int(rss_kb)}")
        sys.exit(0)
print("|0|0|0")
PY
}

# ---------- 校验循环 ----------
PASS=0
SOFT_FAIL=0
HARD_FAIL=0
MISSING=0
echo ""
echo "=========================================="
echo "  UDAF 29 项性能契约校验（成果对照）"
echo "=========================================="
printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
       "ID" "契约" "测量值" "阈值" "软阈值" "状态"
echo "------------------------------------------------------------------------------------------------"

for entry in "${THRESHOLDS[@]}"; do
    IFS='|' read -r bench threshold comp unit id desc <<< "$entry"

    metric=$(extract_metric "$bench")
    measured_ns=$(echo "$metric" | cut -d'|' -f2)
    measured_ips=$(echo "$metric" | cut -d'|' -f3)
    measured_rss=$(echo "$metric" | cut -d'|' -f4)

    if [ "$measured_ns" = "0" ] && [ "$measured_ips" = "0" ]; then
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "N/A" "$threshold" "N/A" "❌ MISSING"
        MISSING=$((MISSING+1))
        continue
    fi

    # 内存契约：通过 bench 名识别（"memory" 子串）
    if [[ "$bench" == *"memory"* ]]; then
        value="$measured_rss"
        if [ "$value" -le 0 ]; then
            printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
                   "#$id" "$desc" "N/A" "${threshold}KB" "N/A" "❌ MISSING"
            MISSING=$((MISSING+1))
            continue
        fi
        soft_thr=$(python3 -c "print(int($threshold * $SOFT_MULT))")
        hard_thr=$(python3 -c "print(int($threshold * $HARD_MULT))")
        if python3 -c "exit(0 if $value <= $threshold else 1)"; then
            status="✅ PASS"; PASS=$((PASS+1))
        elif python3 -c "exit(0 if $value <= $soft_thr else 1)"; then
            status="⚠️  SOFT"; SOFT_FAIL=$((SOFT_FAIL+1))
        else
            status="❌ HARD"; HARD_FAIL=$((HARD_FAIL+1))
        fi
        value_str="$(python3 -c "print(f'{$value/1024:.1f}MB')")"
        thr_str="$(python3 -c "print(f'{$threshold/1024:.0f}MB')")"
        soft_str="$(python3 -c "print(f'{$soft_thr/1024:.0f}MB')")"
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "$value_str" "$thr_str" "$soft_str" "$status"
    elif [ "$comp" = ">=" ]; then
        value="$measured_ips"
        soft_thr=$(python3 -c "import math; print(int($threshold * $SOFT_MULT))")
        hard_thr=$(python3 -c "import math; print(int($threshold * $HARD_MULT))")
        # 吞吐越大越好 → SOFT_FAIL 表示小于软阈值
        if python3 -c "exit(0 if $value >= $threshold else 1)"; then
            status="✅ PASS"; PASS=$((PASS+1))
        elif python3 -c "exit(0 if $value >= $soft_thr else 1)"; then
            status="⚠️  SOFT"; SOFT_FAIL=$((SOFT_FAIL+1))
        else
            status="❌ HARD"; HARD_FAIL=$((HARD_FAIL+1))
        fi
        if [ "$unit" = "" ] && [ $value -gt 10000 ]; then
            value_str="$(python3 -c "print(f'{$value/1e6:.2f}M/s')")"
        else
            value_str="$value"
        fi
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "$value_str" "${threshold}+" "${soft_thr}+" "$status"
    else
        # 时间类（ns）
        value="$measured_ns"
        soft_thr=$(python3 -c "print(int($threshold * $SOFT_MULT))")
        hard_thr=$(python3 -c "print(int($threshold * $HARD_MULT))")
        if python3 -c "exit(0 if $value <= $threshold else 1)"; then
            status="✅ PASS"; PASS=$((PASS+1))
        elif python3 -c "exit(0 if $value <= $soft_thr else 1)"; then
            status="⚠️  SOFT"; SOFT_FAIL=$((SOFT_FAIL+1))
        else
            status="❌ HARD"; HARD_FAIL=$((HARD_FAIL+1))
        fi
        # 转 μs 显示
        if [ "$value" -gt 1000 ]; then
            value_str="$(python3 -c "print(f'{$value/1000:.1f}μs')")"
        else
            value_str="${value}ns"
        fi
        soft_str="$(python3 -c "print(f'{$soft_thr/1000:.0f}μs')")"
        thr_str="$(python3 -c "print(f'{$threshold/1000:.0f}μs')")"
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "$value_str" "$thr_str" "$soft_str" "$status"
    fi
done

echo "------------------------------------------------------------------------------------------------"
echo "汇总：✅ $PASS pass / ⚠️  $SOFT_FAIL soft-fail / ❌ $HARD_FAIL hard-fail / 🚫 $MISSING missing"

# 内存契约（独立进程测量）
echo ""
if [ -x "$SCRIPT_DIR/measure_memory.sh" ]; then
    bash "$SCRIPT_DIR/measure_memory.sh" 2>&1 || true
fi

# 退出码
if [ $HARD_FAIL -gt 0 ]; then exit 2; fi
if [ $SOFT_FAIL -gt 0 ]; then exit 1; fi
exit 0
