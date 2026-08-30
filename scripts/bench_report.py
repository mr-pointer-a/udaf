#!/usr/bin/env python3
"""
bench_report.py - 从 Google Benchmark JSON 输出生成 markdown 报告

用法：
    python3 scripts/bench_report.py /path/to/bench_results.json
    python3 scripts/bench_report.py /path/to/bench_results.json > docs/bench-results.md

输出格式：表格（benchmark | CPU 时间 | 吞吐量 | 测量单位）
"""
import json
import sys
from pathlib import Path


def fmt_cpu(ns: float) -> str:
    """CPU 时间转人类可读格式"""
    if ns < 1000:
        return f"{ns:.0f} ns"
    if ns < 1_000_000:
        return f"{ns/1000:.1f} μs"
    return f"{ns/1_000_000:.2f} ms"


def fmt_ips(ips: float) -> str:
    """吞吐转人类可读格式"""
    if ips < 1000:
        return f"{ips:.0f}/s"
    if ips < 1_000_000:
        return f"{ips/1000:.1f}K/s"
    return f"{ips/1_000_000:.2f}M/s"


def main():
    if len(sys.argv) < 2:
        print("用法：python3 bench_report.py <bench_results.json>", file=sys.stderr)
        sys.exit(1)

    path = Path(sys.argv[1])
    if not path.exists():
        print(f"找不到文件：{path}", file=sys.stderr)
        sys.exit(1)

    data = json.loads(path.read_text())
    benches = data.get("benchmarks", [])

    if not benches:
        print("(无 benchmark 结果)", file=sys.stderr)
        sys.exit(1)

    # 表头
    print("# UDAF 性能基准报告\n")
    print(f"**生成时间**：{data.get('context', {}).get('date', 'N/A')}\n")
    print(f"**CPU**：{data.get('context', {}).get('cpu', 'N/A')}\n")
    print(f"**基准数**：{len(benches)}\n")

    print("## CPU 时间 / 吞吐量\n")
    print("| Benchmark | CPU 时间 | 吞吐量 | 单位 |")
    print("|-----------|---------|--------|------|")
    for b in benches:
        name = b["name"]
        cpu = b.get("cpu_time", 0)
        ips = b.get("items_per_second", 0)
        unit = b.get("time_unit", "ns")
        cpu_str = fmt_cpu(cpu)
        ips_str = fmt_ips(ips)
        # rss_kb counter 用于内存契约
        rss_kb = b.get("rss_kb", None)
        if rss_kb is not None:
            unit_str = f"{rss_kb/1024:.2f} MB"
        else:
            unit_str = unit
        print(f"| `{name}` | {cpu_str} | {ips_str} | {unit_str} |")

    print("\n## 性能契约验证\n")
    print("运行 `bash scripts/check_perf_contracts.sh` 查看 33 项契约校验结果。\n")


if __name__ == "__main__":
    main()