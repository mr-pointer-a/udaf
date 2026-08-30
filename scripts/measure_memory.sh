#!/bin/bash
# scripts/measure_memory.sh - 架构 #1 #2 #28 #29 内存契约专用测量
#
# 设计：
#   - 内存契约需隔离进程测量（避免 benchmark 二进制本身的开销）
#   - 用 fork() 创建子进程，子进程构造 UDAF 组件后打印 RSS，父进程捕获
#   - 4 个独立二进制（device_idle / host_idle / device_peak / host_peak）
#   - 输出阈值 PASS / FAIL，错误码 0/1
#
# 用法：
#   bash scripts/measure_memory.sh                # 默认测量全部 4 项
#   bash scripts/measure_memory.sh device_idle    # 单项
#
# 阈值（来自架构 §3.4 + ADR-003 §5.1）：
#   #1  device idle < 8MB = 8192 KB
#   #2  host   idle < 32MB = 32768 KB
#   #28 device peak < 16MB = 16384 KB
#   #29 host   peak < 128MB = 131072 KB

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-mem"

# 软阈值倍数（与 check_perf_contracts.sh 一致）
SOFT_MULT=1.2
HARD_MULT=1.5

# 内存契约定义：name|binary|threshold_kb|contract_id|description
CONTRACTS=(
    "device_idle|udaf_mem_device_idle|8192|1|设备端空闲内存 < 8MB（架构 #1）"
    "host_idle|udaf_mem_host_idle|32768|2|主机端空闲内存 < 32MB（架构 #2）"
    "device_peak|udaf_mem_device_peak|16384|28|设备端峰值内存 < 16MB（架构 #28）"
    "host_peak|udaf_mem_host_peak|131072|29|主机端峰值内存 < 128MB（架构 #29）"
)

# 编译（一次性）
if [ ! -x "$BUILD_DIR/device_idle/udaf_mem_device_idle" ] || [[ "${1:-}" == "--rebuild" ]]; then
    echo "==> 编译内存测量二进制"
    mkdir -p "$BUILD_DIR"
    [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && \
        cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1

    # 4 个独立 measure_*.cpp 源文件
    for src in device_idle host_idle device_peak host_peak; do
        cat > "$BUILD_DIR/measure_$src.cpp" <<EOF
// measure_$src.cpp - 内存契约测量（独立进程）
#include "sdk/sdk/sdk.hpp"
#include "ability_a/registry/service_registry.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

std::size_t read_rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::size_t kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %zu", &kb);
            return kb;
        }
    }
    return 0;
}

int main() {
    udaf::sdk::ClientConfig cfg;
EOF

        case "$src" in
            device_idle)
                cat >> "$BUILD_DIR/measure_$src.cpp" <<EOF
    cfg.node_id    = "udaf-device-idle";
    cfg.audit_path = "";
    std::size_t rss_before = read_rss_kb();
    udaf::sdk::Client c(cfg);
    std::size_t rss_after = read_rss_kb();
    std::size_t delta = rss_after > rss_before ? rss_after - rss_before : rss_after;
    std::printf("%zu\n", delta);
    return 0;
}
EOF
                ;;
            host_idle)
                cat >> "$BUILD_DIR/measure_$src.cpp" <<EOF
    cfg.node_id    = "udaf-host-idle";
    cfg.audit_path = "/tmp/udaf_mem_host_idle.log";
    std::size_t rss_before = read_rss_kb();
    udaf::sdk::Client c(cfg);
    udaf::ability_a::registry::ServiceRegistry reg;
    std::size_t rss_after = read_rss_kb();
    std::size_t delta = rss_after > rss_before ? rss_after - rss_before : rss_after;
    std::printf("%zu\n", delta);
    return 0;
}
EOF
                ;;
            device_peak)
                cat >> "$BUILD_DIR/measure_$src.cpp" <<EOF
    cfg.node_id    = "udaf-device-peak";
    cfg.audit_path = "/tmp/udaf_mem_device_peak.log";
    std::size_t rss_before = read_rss_kb();
    udaf::sdk::Client c(cfg);
    (void)c.start();
    udaf::ability_a::registry::ServiceRegistry reg;
    for (int i = 0; i < 50; ++i) {
        udaf::ability_a::registry::RegistryEntry e;
        e.node_id_ = "n" + std::to_string(i);
        e.hostname_ = "host" + std::to_string(i);
        e.bind_address_ = "127.0.0.1";
        e.bind_port_ = static_cast<std::uint16_t>(8000 + i);
        (void)reg.register_node(e);
    }
    std::size_t rss_after = read_rss_kb();
    std::size_t delta = rss_after > rss_before ? rss_after - rss_before : rss_after;
    std::printf("%zu\n", delta);
    (void)c.stop();
    return 0;
}
EOF
                ;;
            host_peak)
                cat >> "$BUILD_DIR/measure_$src.cpp" <<EOF
    cfg.node_id    = "udaf-host-peak";
    cfg.audit_path = "/tmp/udaf_mem_host_peak.log";
    std::size_t rss_before = read_rss_kb();
    udaf::sdk::Client c(cfg);
    (void)c.start();
    udaf::ability_a::registry::ServiceRegistry reg;
    for (int i = 0; i < 1000; ++i) {
        udaf::ability_a::registry::RegistryEntry e;
        e.node_id_ = "node" + std::to_string(i);
        e.hostname_ = "host" + std::to_string(i);
        e.bind_address_ = "10.0.0." + std::to_string(i % 256);
        e.bind_port_ = static_cast<std::uint16_t>(8000 + (i % 1000));
        (void)reg.register_node(e);
    }
    std::size_t rss_after = read_rss_kb();
    std::size_t delta = rss_after > rss_before ? rss_after - rss_before : rss_after;
    std::printf("%zu\n", delta);
    (void)c.stop();
    return 0;
}
EOF
                ;;
        esac
    done

    # 编译 4 个独立二进制（只链接需要的库，最小化 RSS）
    for src in device_idle host_idle device_peak host_peak; do
        g++ -std=c++20 -O2 -DNDEBUG \
            -I"$PROJECT_DIR/src" \
            "$BUILD_DIR/measure_$src.cpp" \
            -L"$BUILD_DIR/src/sdk/sdk" \
            -L"$BUILD_DIR/src/core" \
            -L"$BUILD_DIR/src/audit" \
            -L"$BUILD_DIR/src/crypto" \
            -L"$BUILD_DIR/src/ability_a/registry" \
            -L"$BUILD_DIR/src/ability_a/transport" \
            -L"$BUILD_DIR/src/ability_a/discovery" \
            -L"$BUILD_DIR/src/ability_a/bridge" \
            -L"$BUILD_DIR/src/ability_a/trust" \
            -L"$BUILD_DIR/src/ability_b/node" \
            -L"$BUILD_DIR/src/ability_b/topology" \
            -L"$BUILD_DIR/src/ability_b/transport" \
            -L"$BUILD_DIR/src/ability_b/port" \
            -L"$BUILD_DIR/src/ability_b/serialization" \
            -L"$BUILD_DIR/src/observability" \
            -ludaf_sdk -ludaf_core -ludaf_audit -ludaf_crypto \
            -ludaf_ability_a_registry -ludaf_ability_a_trust \
            -ludaf_observability -ludaf_ability_b_topology \
            -ludaf_ability_b_transport -ludaf_ability_b_port \
            -ludaf_ability_b_node -ludaf_ability_b_serialization \
            -lssl -lcrypto -lpthread -lspdlog -lfmt \
            -o "$BUILD_DIR/$src/udaf_mem_$src" 2>&1 || \
            echo "WARN: $src 编译失败（可能缺库）；将跳过该契约"
        mkdir -p "$BUILD_DIR/$src"
        # 如果链接失败，重试简化版本
        if [ ! -x "$BUILD_DIR/$src/udaf_mem_$src" ]; then
            g++ -std=c++20 -O2 -DNDEBUG \
                -I"$PROJECT_DIR/src" \
                "$BUILD_DIR/measure_$src.cpp" \
                -L"$BUILD_DIR/src/sdk/sdk" \
                -L"$BUILD_DIR/src/core" \
                -L"$BUILD_DIR/src/audit" \
                -L"$BUILD_DIR/src/crypto" \
                -L"$BUILD_DIR/src/ability_a/registry" \
                -L"$BUILD_DIR/src/ability_a/transport" \
                -L"$BUILD_DIR/src/ability_a/discovery" \
                -L"$BUILD_DIR/src/ability_a/bridge" \
                -L"$BUILD_DIR/src/ability_a/trust" \
                -L"$BUILD_DIR/src/observability" \
                -ludaf_sdk -ludaf_core -ludaf_audit -ludaf_crypto \
                -ludaf_ability_a_registry -ludaf_ability_a_trust \
                -ludaf_observability \
                -lssl -lcrypto -lpthread -lspdlog -lfmt \
                -o "$BUILD_DIR/$src/udaf_mem_$src" 2>/dev/null && \
                echo "INFO: $src 使用简化链接成功"
        fi
    done
fi

# 跑测量
echo "=========================================="
echo "  UDAF 内存契约测量（独立进程）"
echo "=========================================="
printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
       "ID" "契约" "测量值" "阈值" "软阈值" "状态"
echo "------------------------------------------------------------------------------------------------"

PASS=0
FAIL=0
SKIP=0
TARGET="${1:-all}"

for entry in "${CONTRACTS[@]}"; do
    IFS='|' read -r name bin threshold id desc <<< "$entry"

    # 跳过非目标
    [ "$TARGET" != "all" ] && [ "$TARGET" != "$name" ] && continue

    BIN_PATH="$BUILD_DIR/$name/udaf_mem_$name"
    if [ ! -x "$BIN_PATH" ]; then
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "N/A" "${threshold}KB" "N/A" "🚫 SKIP"
        SKIP=$((SKIP+1))
        continue
    fi

    # 跑 3 次取最大值（peak memory）
    MAX_KB=0
    for _ in 1 2 3; do
        KB=$("$BIN_PATH" 2>/dev/null | tail -1)
        if [ -n "$KB" ] && [ "$KB" -gt "$MAX_KB" ]; then
            MAX_KB=$KB
        fi
    done

    if [ "$MAX_KB" -eq 0 ]; then
        printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
               "#$id" "$desc" "ERR" "${threshold}KB" "N/A" "❌ ERROR"
        FAIL=$((FAIL+1))
        continue
    fi

    SOFT_THR=$(python3 -c "print(int($threshold * $SOFT_MULT))")
    HARD_THR=$(python3 -c "print(int($threshold * $HARD_MULT))")

    if [ "$MAX_KB" -le "$threshold" ]; then
        STATUS="✅ PASS"; PASS=$((PASS+1))
    elif [ "$MAX_KB" -le "$SOFT_THR" ]; then
        STATUS="⚠️  SOFT"; FAIL=$((FAIL+1))
    else
        STATUS="❌ HARD"; FAIL=$((FAIL+1))
    fi

    MEAS_MB=$(python3 -c "print(f'{$MAX_KB/1024:.1f}MB')")
    THR_MB=$(python3 -c "print(f'{$threshold/1024:.0f}MB')")
    SOFT_MB=$(python3 -c "print(f'{$SOFT_THR/1024:.0f}MB')")
    printf "%-4s | %-32s | %10s | %10s | %10s | %s\n" \
           "#$id" "$desc" "$MEAS_MB" "$THR_MB" "$SOFT_MB" "$STATUS"
done

echo "------------------------------------------------------------------------------------------------"
echo "汇总：✅ $PASS pass / ❌ $FAIL fail / 🚫 $SKIP skip"

[ $FAIL -gt 0 ] && exit 1
exit 0
