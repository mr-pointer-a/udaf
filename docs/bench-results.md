# UDAF 性能基准报告

**生成时间**：2026-08-31T05:40:11+08:00

**CPU**：N/A

**基准数**：35

## CPU 时间 / 吞吐量

| Benchmark | CPU 时间 | 吞吐量 | 单位 |
|-----------|---------|--------|------|
| `udaf_bench_registry_snapshot_10k` | 329.5 μs | 30.35M/s | ns |
| `udaf_bench_inproc_throughput` | 43 ns | 23.32M/s | ns |
| `udaf_bench_inproc_latency_p95/manual_time` | 106 ns | 0/s | ns |
| `udaf_bench_psk_handshake_p95/manual_time` | 35.9 μs | 0/s | ns |
| `udaf_bench_fork_exec` | 97.4 μs | 0/s | ns |
| `udaf_bench_audit_throughput` | 14.0 μs | 71.7K/s | ns |
| `udaf_bench_result_ok` | 6 ns | 180.07M/s | ns |
| `udaf_bench_channel_reopen` | 335 ns | 2.99M/s | ns |
| `udaf_bench_channel_send_recv` | 38 ns | 26.51M/s | ns |
| `udaf_bench_channel_heartbeat_priority` | 37 ns | 26.95M/s | ns |
| `udaf_bench_port_try_recv` | 7 ns | 152.41M/s | ns |
| `udaf_bench_port_try_send` | 19 ns | 51.77M/s | ns |
| `udaf_bench_topology_add_node` | 619 ns | 1.61M/s | ns |
| `udaf_bench_pki_handshake/manual_time` | 20.4 μs | 0/s | ns |
| `udaf_bench_wal_append` | 14.9 μs | 67.3K/s | ns |
| `udaf_bench_subscribe_fire` | 578 ns | 1.73M/s | ns |
| `udaf_bench_subscribe_batch` | 15.0 μs | 6.66M/s | ns |
| `udaf_bench_hmac_single` | 1.5 μs | 688.4K/s | ns |
| `udaf_bench_large_msg_1mb` | 782.1 μs | 0/s | ns |
| `udaf_bench_default_msg_4kb` | 101 ns | 0/s | ns |
| `udaf_bench_heart_aggregate_100` | 1.9 μs | 0/s | ns |
| `udaf_bench_node_cold_startup/manual_time` | 28.2 μs | 0/s | ns |
| `udaf_bench_node_reload` | 2 ns | 0/s | ns |
| `udaf_bench_crypto_overhead` | 1.5 μs | 676.6K/s | ns |
| `udaf_bench_meter_observe` | 33 ns | 30.64M/s | ns |
| `udaf_bench_prom_export` | 17.2 μs | 0/s | ns |
| `udaf_bench_aead_per_frame` | 1.3 μs | 0/s | ns |
| `udaf_bench_wal_recovery_1000` | 29.7 μs | 0/s | ns |
| `udaf_bench_observability_overhead_baseline` | 0 ns | 0/s | ns |
| `udaf_bench_observability_overhead_enabled` | 30 ns | 0/s | ns |
| `udaf_bench_max_concurrent_nodes_1000` | 3.0 μs | 0/s | ns |
| `udaf_bench_device_peak_memory` | 72.3 μs | 0/s | 50.65 MB |
| `udaf_bench_host_peak_memory` | 345.6 μs | 0/s | 50.77 MB |
| `udaf_bench_device_idle_memory` | 22.4 μs | 0/s | 50.77 MB |
| `udaf_bench_host_idle_memory` | 28.4 μs | 0/s | 50.77 MB |

## 性能契约验证

运行 `bash scripts/check_perf_contracts.sh` 查看 33 项契约校验结果。

