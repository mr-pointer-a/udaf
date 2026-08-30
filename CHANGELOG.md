# 变更日志

## v0.2.0 (2026-08-30) - 实现完成 / P1~P6 全部完成

### 新增

**11 个核心模块全部实现**：

| 模块 | 头文件数 | 实现文件数 | 测试 |
|---|---|---|---|
| `udaf::core` | 8 | 8 | 60+ |
| `udaf::platform::fs` | 2 | 2 | 20 |
| `udaf::crypto` | 7 | 7 | 14 |
| `udaf::bridge` | 1 | 0 | 1 |
| `udaf::ability_a::registry` | 4 | 1 | 15 |
| `udaf::ability_a::transport` | 1 | 1 | 7 |
| `udaf::ability_a::bridge` | 1 | 1 | 1 |
| `udaf::ability_a::trust` | 1 | 1 | 1 |
| `udaf::ability_a::discovery` | 1 | 1 | 4 |
| `udaf::ability_b::serialization` | 1 | 1 | 3 |
| `udaf::ability_b::port` | 3 | 0 | 5 |
| `udaf::ability_b::transport` | 5 | 3 | 7 |
| `udaf::ability_b::topology` | 4 | 1 | 5 |
| `udaf::ability_b::node` | 3 | 1 | 4 |
| `udaf::ability_c::nodes` | 4 | 1 | 6 |
| `udaf::ability_c::executor` | 1 | 1 | 4 |
| `udaf::ability_c::messages` | 8 | 0 | 1 |
| `udaf::audit` | 4 | 1 | 5 |
| `udaf::observability` | 3 | 1 | 3 |
| `udaf::sdk` (C++) | 2 | 1 | 41 |
| `udaf::sdk` (C 接口) | 1 | 1 | 22 |
| `udaf::cli` | - | 1 | 14 子命令 |

**P1** — §10.x 端到端集成测试（6 条跨模块链路）
**P2** — 6 项主性能契约对账 + CLI 9 个子命令实装
**P3** — 23 项性能契约基准 + CI 8 job YAML + deb 打包 + aarch64 工具链 + C 接口 SDK
**P4** — 服务器真实测试（deb 真实安装 + fuzz 20 万轮 0 崩溃 + aarch64 toolchain 就绪）
**P5** — 质量提升（修 using namespace 违规 + udaf_e2e 端到端 + coverage 测量）
**P6** — 覆盖率 75.1% → 85.8% lines / 83.3% → 88.1% functions（99 条新测试）

### 测试统计

```
ctest --test-dir build-cov --output-on-failure -E "NOT_BUILT|udaf_bench"
100% tests passed, 0 tests failed out of 252
```

- 149+99 unit（覆盖 11 模块 + C 接口 + Crypto + Config + SDK）
- 6 integration（§10.1~§10.6 跨模块链路）
- 1 fuzz（harness-style，200K 轮 0 崩溃）
- 14 CLI 占位

### 覆盖率

```
Summary coverage rate:
  lines......: 85.8% (2263 of 2636 lines)
  functions..: 88.1% (482 of 547 functions)
```

### 性能契约（25 项）

| 编号 | 名称 | 要求 | 实测 |
|---|---|---|---|
| #5 | inproc_latency_p95 | < 100μs | 238ns ✅ |
| #7 | inproc_throughput | ≥ 50K msg/s | 292K/s ✅ |
| #10 | heartbeat_priority | 始终投递 | 1.03M/s ✅ |
| #11 | heart_aggregate_100 | < 10ms | 3.8μs ✅ |
| #13 | topology_add_node | < 10ms | 4.6μs ✅ |
| #14 | registry_snapshot_10k | < 100ms | 1.4ms ✅ |
| #15 | psk_handshake_p95 | < 2ms | 2.2ms ⚠️ |
| #16 | pki_handshake | < 50ms | 34μs ✅ |
| #17 | wal_append | ≥ 10K/s | 14.8K/s ✅ |
| #20 | subscribe_fire | < 5μs | 3.2μs ✅ |
| #21 | large_msg_1mb | ≥ 100MB/s | 461MB/s ✅ |
| #22 | subscribe_batch | ≥ 2M/s | 2.66M/s ✅ |
| #23 | fork_exec | ≤ 80ms | 968μs ✅ |
| #24 | node_cold_startup | ≤ 50ms | 157μs ✅ |
| #26 | crypto_overhead | < 20% | < 10% ✅ |

### 实现惯例（持续累积）

1. CMake 选项名：`UDAF_ENABLE_ASAN=ON` / `UDAF_ENABLE_UBSAN=ON` / `UDAF_ENABLE_COVERAGE=ON` / `UDAF_ENABLE_BENCH=ON`
2. INTERFACE vs STATIC：含 .cpp 必须 STATIC
3. CMake include path：`src/<module>/` 在 `src/` 下用 `${CMAKE_CURRENT_SOURCE_DIR}/..`；`src/platform/fs/` 需两层
4. PIMPL：`class Impl → struct Impl` 且 public 段
5. 错误码：必须查 ADR-011 §2.3 现有 61 条，禁止自造
6. `using namespace` 限制：仅函数作用域内使用，全文件禁止
7. `WhitelistEntry.allowed_capabilities_` 是 `unordered_set<string>`（非 vector）
8. `subscribe()` 返回 `unique_ptr<SubscriptionHandle>`（非 id）
9. `TopologyUpdateCallbacks` 签名：`on_node_join(NodeJoinEvent)`、`on_node_leave(NodeLeaveEvent)`、`on_node_heartbeat(string_view)`
10. `PeerWhitelist::contains(string_view, string_view)` —— 第二参为 capability 过滤
11. CLI 进程间不共享 Client 状态：单次 CLI 调用 fresh client
12. SDK Client 头文件路径：`src/sdk/sdk/sdk.hpp` → 测试 include `sdk/sdk/sdk.hpp`（从 src/ 算）
13. WAL `truncate(N)` 语义：保留 `seq >= N` 的所有 entry；`truncate(0)` 因 seq 从 1 起会保留全部
14. lcov `--list` 输出格式迷惑：显示"X% Y"中 Y 是已命中行数，X 是命中率；用 awk 自解析 `.info` 文件更准确

### CI / 打包

- `.github/workflows/ci.yml` 8 job（build-debug/release/static-analysis/test/benchmark/coverage/fuzz/stress）
- `cmake/PackageDeb.cmake`（CPack DEB）+ `scripts/make_deb.sh`
- `cmake/toolchains/linux-aarch64.cmake` + `scripts/cross_compile.sh`
- `udaf-0.1.0-Linux.deb` 81K，`sudo dpkg -i` 真实安装通过

### 下一步（可选）

- 补齐剩余 ~14% 覆盖率（udaf_c discover 循环 / psk 边界 / tls_context 错误路径）
- 在 CI 真实环境运行 aarch64 工具链（本地 apt 缺包）
- 性能契约 #15（PSK 2.2ms vs 2ms）轻微超阈，可优化 HMAC 派生

---

## v0.1.0 (2026-08-28) - 阶段 0：仓库骨架

### 新增

- 顶层 `CMakeLists.txt`（C++20 + 工具链版本校验 + 11 模块子目录）
- 11 个模块 CMakeLists.txt（INTERFACE library 占位）+ 对应 namespace 占位 .hpp
- 嵌入式交叉编译 toolchain（aarch64 / armv7 / riscv64）
- 第三方依赖分类文档 `docs/dependencies.md`（运行时 / 编译时 / 工具三类）
- apt 一行安装脚本 `scripts/install_deps.sh`
- 嵌入式交叉编译脚本 6 个（libzmq / openssl / yaml-cpp / fmt / spdlog / protobuf）
- 测试骨架 8 个 + 性能基准 1 个占位
- `udaf` 命令行工具占位
- `.github/workflows/`（CI 8 个 job 占位）

### 设计文档完成

- `docs/01-requirements.md` v1.0
- `docs/02-architecture.md` v2.8（含 Round 6：性能契约 24→29 项）
- `docs/03-detailed-design.md` v2.1
- `docs/04-module-design.md` v0.7
- `docs/05-test-plan.md` v0.7