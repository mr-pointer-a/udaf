# UDAF 性能基准报告

**生成时间**：2026-08-31T22:48:35+08:00

**CPU**：2 × 2494.13 MHz

**基准数**：35

## CPU 时间 / 吞吐量

| Benchmark | CPU 时间 | 吞吐量 | 单位 |
|-----------|---------|--------|------|
| `udaf_bench_registry_snapshot_10k` | 320.1 μs | 31.24M/s | ns |
| `udaf_bench_inproc_throughput` | 39 ns | 25.54M/s | ns |
| `udaf_bench_inproc_latency_p95/manual_time` | 105 ns | 0/s | ns |
| `udaf_bench_psk_handshake_p95/manual_time` | 35.7 μs | 0/s | ns |
| `udaf_bench_fork_exec` | 96.7 μs | 0/s | ns |
| `udaf_bench_audit_throughput` | 15.1 μs | 66.38K/s | ns |
| `udaf_bench_result_ok` | 6 ns | 175.24M/s | ns |
| `udaf_bench_channel_reopen` | 370 ns | 2.71M/s | ns |
| `udaf_bench_channel_send_recv` | 40 ns | 24.87M/s | ns |
| `udaf_bench_channel_heartbeat_priority` | 40 ns | 25.25M/s | ns |
| `udaf_bench_port_try_recv` | 8 ns | 132.91M/s | ns |
| `udaf_bench_port_try_send` | 22 ns | 46.22M/s | ns |
| `udaf_bench_topology_add_node` | 717 ns | 1.39M/s | ns |
| `udaf_bench_pki_handshake/manual_time` | 24.5 μs | 0/s | ns |
| `udaf_bench_wal_append` | 16.0 μs | 62.33K/s | ns |
| `udaf_bench_subscribe_fire` | 629 ns | 1.59M/s | ns |
| `udaf_bench_subscribe_batch` | 14.8 μs | 6.73M/s | ns |
| `udaf_bench_hmac_single` | 1.4 μs | 710.61K/s | ns |
| `udaf_bench_large_msg_1mb` | 695.8 μs | 0/s | ns |
| `udaf_bench_default_msg_4kb` | 90 ns | 0/s | ns |
| `udaf_bench_heart_aggregate_100` | 1.6 μs | 0/s | ns |
| `udaf_bench_node_cold_startup/manual_time` | 27.4 μs | 0/s | ns |
| `udaf_bench_node_reload` | 1 ns | 0/s | ns |
| `udaf_bench_crypto_overhead` | 1.4 μs | 740.39K/s | ns |
| `udaf_bench_meter_observe` | 31 ns | 32.28M/s | ns |
| `udaf_bench_prom_export` | 16.7 μs | 0/s | ns |
| `udaf_bench_aead_per_frame` | 1.3 μs | 0/s | ns |
| `udaf_bench_wal_recovery_1000` | 30.7 μs | 0/s | ns |
| `udaf_bench_observability_overhead_baseline` | 0 ns | 0/s | ns |
| `udaf_bench_observability_overhead_enabled` | 30 ns | 0/s | ns |
| `udaf_bench_max_concurrent_nodes_1000` | 2.6 μs | 0/s | ns |
| `udaf_bench_device_peak_memory` | 71.3 μs | 0/s | 51.86 MB |
| `udaf_bench_host_peak_memory` | 348.1 μs | 0/s | 51.98 MB |
| `udaf_bench_device_idle_memory` | 22.5 μs | 0/s | 51.98 MB |
| `udaf_bench_host_idle_memory` | 28.1 μs | 0/s | 51.98 MB |

## 性能契约验证

运行 `bash scripts/check_perf_contracts.sh` 查看 29 项契约校验结果。

