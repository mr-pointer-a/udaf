# UDAF 性能基准报告

**生成时间**：2026-09-01T00:06:56+08:00

**CPU**：2 × 2494.13 MHz

**基准数**：39

## CPU 时间 / 吞吐量

| Benchmark | CPU 时间 | 吞吐量 | 单位 |
|-----------|---------|--------|------|
| `udaf_bench_registry_snapshot_10k` | 1432.4 μs | 6.98M/s | items |
| `udaf_bench_inproc_throughput` | 2.8 μs | 361.14K/s | items |
| `udaf_bench_inproc_latency_p95/manual_time` | 252 ns | 0/s | manual_time |
| `udaf_bench_psk_handshake_p95/manual_time` | 2228.0 μs | 0/s | manual_time |
| `udaf_bench_fork_exec` | 937.6 μs | 0/s | time |
| `udaf_bench_audit_throughput` | 92.3 μs | 10.83K/s | items |
| `udaf_bench_result_ok` | 0.9 μs | 1.22M/s | items |
| `udaf_bench_channel_reopen` | 5.2 μs | 193.61K/s | items |
| `udaf_bench_channel_send_recv` | 1.0 μs | 1.00M/s | items |
| `udaf_bench_channel_heartbeat_priority` | 0.9 μs | 1.11M/s | items |
| `udaf_bench_port_try_recv` | 0.8 μs | 1.28M/s | items |
| `udaf_bench_port_try_send` | 0.9 μs | 1.15M/s | items |
| `udaf_bench_topology_add_node` | 4.7 μs | 215.29K/s | items |
| `udaf_bench_pki_handshake/manual_time` | 30.0 μs | 0/s | manual_time |
| `udaf_bench_wal_append` | 43.1 μs | 23.19K/s | items |
| `udaf_bench_subscribe_fire` | 2.5 μs | 415.46K/s | items |
| `udaf_bench_subscribe_batch` | 34.8 μs | 2.87M/s | items |
| `udaf_bench_hmac_single` | 8.0 μs | 125.41K/s | items |
| `udaf_bench_large_msg_1mb` | 2065.9 μs | 483.91 MiB/s | bytes |
| `udaf_bench_default_msg_4kb` | 1.8 μs | 0/s | time |
| `udaf_bench_heart_aggregate_100` | 3.2 μs | 0/s | time |
| `udaf_bench_node_cold_startup/manual_time` | 157.0 μs | 0/s | manual_time |
| `udaf_bench_node_reload` | 0.9 μs | 0/s | time |
| `udaf_bench_crypto_overhead` | 8.3 μs | 121.17K/s | items |
| `udaf_bench_meter_observe` | 1.0 μs | 1.00M/s | items |
| `udaf_bench_prom_export` | 23.3 μs | 0/s | time |
| `udaf_bench_aead_per_frame` | 3.1 μs | 0/s | time |
| `udaf_bench_wal_recovery_1000` | 41.5 μs | 0/s | time |
| `udaf_bench_observability_overhead_baseline` | 0.9 μs | 0/s | time |
| `udaf_bench_observability_overhead_enabled` | 0.9 μs | 0/s | time |
| `udaf_bench_max_concurrent_nodes_1000` | 3.9 μs | 0/s | time |
| `udaf_bench_device_peak_memory` | 131.4 μs | 0/s | rss_kb=11728 |
| `udaf_bench_host_peak_memory` | 438.9 μs | 0/s | rss_kb=128 |
| `udaf_bench_device_idle_memory` | 28.3 μs | 0/s | rss_kb=11856 |
| `udaf_bench_host_idle_memory` | 45.1 μs | 0/s | rss_kb=11856 |
| `udaf_bench_aead_throughput_1mb` | 850.1 μs | 1.15 GiB/s | bytes |
| `udaf_bench_audit_verify_chain` | 64.5 μs | 0/s | time |
| `udaf_bench_wal_replay_full` | 1620.9 μs | 0/s | time |
| `udaf_bench_topology_commit_50` | 22.3 μs | 2.26M/s | items |

## 性能契约验证

v0.3.13 扩展至 **39 项性能契约**（29 → 39，新增 4 项大块吞吐 / 链验证 / 拓扑事务批量）。

| 类别 | 数量 | 代表契约 |
|---|---|---|
| 加密 | 5 | PSK 握手 p95、PKI 握手、HMAC 单次、AEAD 单帧/1MB、加密开销 |
| 通道 | 5 | inproc 吞吐/延迟、重连、heartbeat 优先级、send/recv |
| 拓扑 | 2 | add_node、commit 50 节点批量 |
| 节点 | 3 | fork+exec、冷启动、reload |
| WAL | 3 | append、recovery 1000、完整 append+replay |
| 审计 | 2 | throughput、verify_chain |
| 端口 | 2 | try_recv、try_send |
| 注册中心 | 4 | snapshot 10k、subscribe_fire、subscribe_batch、max_concurrent_nodes_1000 |
| 可观测性 | 4 | meter_observe、prom_export、overhead baseline/enabled |
| 内存峰值 | 4 | device/host × peak/idle |
| 序列化 | 2 | large_msg_1mb、default_msg_4kb |
| 心跳 | 1 | heart_aggregate_100 |
| 结果类型 | 1 | result_ok |
| **合计** | **39** | |

运行 `bash scripts/check_perf_contracts.sh` 查看契约校验结果。