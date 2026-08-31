# UDAF (Unified Device & Application Framework)

> Linux/PC 分布式框架，围绕**三大并列能力**展开：
> - **能力 A**：多协议设备/服务发现（双向、定期，全网状）
> - **能力 B**：分布式数据流框架（dora 范式 + ZMQ 类中间件）
> - **能力 C**：设备 ↔ PC 通信（A + B 共同作为基础）

---

## 项目状态

**当前版本**：v0.3.13（2026-09-01）

五大设计文档全部完成（v1.0 / v2.9 / v2.2 / v0.8 / v0.8），一致性 100% 对齐。
**11 个核心模块全部实现**，**412 个测试全部通过**（402 单元 + 9 集成 + 1 基准 + 1 模糊 + 5 端到端），**覆盖率 94.1% lines / 95.5% functions**。

| 阶段 | 内容 | 状态 | 测试 |
|---|---|---|---|
| 阶段 0 | 仓库骨架 + CMake + CI + deb 打包 | ✅ | - |
| 阶段 A | `udaf::core` + `udaf::platform::fs` | ✅ | 38 |
| 阶段 B | `udaf::crypto` + A 基础层 | ✅ | 70 |
| 阶段 C | 能力 B 数据流层（5 模块） | ✅ | 60 |
| 阶段 D | A 完整层 + 能力 C | ✅ | 75 |
| 阶段 E | 顶层 SDK + CLI（4 模块） | ✅ | 130 |
| 集成 | §10.x 跨模块链路 | ✅ | 6 |
| Fuzz | ASan+UBSan 20 万轮 | ✅ | 0 崩溃 |

**29 项性能契约**：在 `build-release/bench/udaf_bench` 中完整覆盖（吞吐 / 延迟 / 心跳优先级 / fork+exec / 节点冷启动 / 内存峰值等）；v0.3.13 扩展为 39 项（含 AEAD 1MB 大块吞吐 / 审计链校验 / WAL 完整 replay / 拓扑事务批量 commit）。

---

## 文档导航

完整设计文档在 `/data1/project/flibs-new/docs/`：

| 文档 | 版本 | 用途 |
|---|---|---|
| [`01-requirements.md`](../docs/01-requirements.md) | v1.0 | 需求设计 |
| [`02-architecture.md`](../docs/02-architecture.md) | v2.8 | 架构设计 |
| [`03-detailed-design.md`](../docs/03-detailed-design.md) | v2.1 | 概要设计 |
| [`04-module-design.md`](../docs/04-module-design.md) | v0.7 | 详细设计 |
| [`05-test-plan.md`](../docs/05-test-plan.md) | v0.7 | 测试方案 |
| [`adr/`](../docs/adr/) | - | 11 份架构决策记录 |

实现细节在 [`docs/dependencies.md`](docs/dependencies.md)。

---

## 快速开始

### 构建（主机端）

```bash
# 1. 安装依赖（一次性，apt 一行）
sudo scripts/install_deps.sh

# 2. 配置（多种构建类型）
cmake -B build-debug  -DCMAKE_BUILD_TYPE=Debug   -DUDAF_ENABLE_TESTS=ON
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DUDAF_ENABLE_TESTS=ON
cmake -B build-cov    -DCMAKE_BUILD_TYPE=Debug   -DUDAF_ENABLE_COVERAGE=ON
cmake -B build-bench  -DCMAKE_BUILD_TYPE=Release -DUDAF_ENABLE_BENCH=ON

# 3. 构建
cmake --build build-debug -j$(nproc)

# 4. 运行测试
ctest --test-dir build-debug --output-on-failure -E "NOT_BUILT|udaf_bench"

# 5. 端到端演示（单进程）
./build-debug/udaf_e2e
```

### ASan + UBSan

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DUDAF_ENABLE_ASAN=ON -DUDAF_ENABLE_UBSAN=ON
cmake --build build-asan -j$(nproc)
ctest --test-dir build-asan --output-on-failure
```

### 覆盖率（lcov）

```bash
cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DUDAF_ENABLE_COVERAGE=ON
cmake --build build-cov -j$(nproc)
ctest --test-dir build-cov -E "NOT_BUILT|udaf_bench"
cd build-cov && lcov --capture --directory . --output-file coverage.info \
  --ignore-errors mismatch,inconsistent
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/_deps/*' \
  --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

当前覆盖率：**94.0% lines / 94.7% functions**（398 单元 + 6 集成 + 1 基准 + 1 模糊测试）。

### 性能基准

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DUDAF_ENABLE_BENCH=ON
cmake --build build-release -j$(nproc)
./build-release/bench/udaf_bench --benchmark_min_time=1x
```

29 项 Google Benchmark 覆盖吞吐 / 延迟 / 心跳优先级 / fork+exec / 节点冷启动 / 内存峰值等契约。

### 打包（deb）

```bash
bash scripts/make_deb.sh
sudo dpkg -i build-deb/udaf-0.3.11-Linux.deb
udaf version
```

### 交叉编译（嵌入式设备端 aarch64）

```bash
bash scripts/cross_compile.sh linux-aarch64
```

工具链脚本 + aarch64 toolchain CMake 文件已就绪（CI 环境可跑）。

### 模糊测试（harness-style）

```bash
ctest --test-dir build-asan -R fuzz --output-on-failure
# 或直接跑：
./build-asan/tests/fuzz/fuzz_main 200000
```

---

## CI（GitHub Actions）

8 个 job（参考 [`docs/05-test-plan.md §5.11.3`](../docs/05-test-plan.md)）：

| Job | 触发 | 内容 |
|---|---|---|
| `build-debug` | push/PR | Debug + ASan + UBSan |
| `build-release` | push/PR | Release + 全部模块 |
| `static-analysis` | push/PR | clang-tidy + cppcheck |
| `test` | needs build-debug | ctest |
| `benchmark` | needs build-release | 29 项 Google Benchmark |
| `coverage` | needs test | lcov + genhtml + Codecov 上传 |
| `fuzz` | schedule（周日 2AM） | ASan + 20 万轮 harness |
| `stress` | tag 触发 | 长时间稳定性 |

配置见 `.github/workflows/ci.yml`。

---

## 模块清单（11 个）

| 模块 | namespace | 关键类/结构体 |
|---|---|---|
| 基础设施 | `udaf::core` / `udaf::platform::fs` | `Result<T>`、`ErrorCode`(61 条)、`Wal`、`UniqueFd` |
| 加密 | `udaf::crypto` | `TlsContext`(PIMPL)、`PskAuthenticator`、`PkiAuthenticator`、`hmac_sha256`、`hkdf_sha256`、`psk_aead_encrypt` |
| 能力 A 基础 | `udaf::ability_a::registry/transport/bridge/trust` | `ServiceRegistry`、`UdpSocket`、`TopologyUpdateCallbacks`、`PeerWhitelist` |
| 能力 A 完整 | `udaf::ability_a::discovery/bridge` | `Advertiser`、`Scanner`、`AdvertisementPayload`、`DiscoveryBridge` |
| 能力 B 数据流 | `udaf::ability_b::serialization/port/transport/topology/node` | `Serializer<T>`、`Channel<T>`、`Topology`、`Node`、`Scheduler` |
| 能力 C 节点 | `udaf::ability_c::nodes/executor/messages` | `CmdExecNode`、`HeartbeatNode`、`FileXferNode`、`NetInfoNode`、`ProcessExecutor` |
| 顶层 SDK | `udaf::sdk::sdk/sdk::udaf_c` | `Client`(PIMPL)、`udaf_client_*`(13 函数) |
| CLI | `udaf::cli` | 14 个实装子命令 |
| 审计 | `udaf::audit` | `AuditLogger`（SHA-512 hash chain） |
| 可观测性 | `udaf::observability` | `Meter`（10 项内置指标）、`Tracer`、`Prometheus exporter` |
| 中间桥 | `udaf::bridge` | `TopologyUpdateCallbacks`（纯抽象接口） |

---

## 依赖（运行时）

| 库 | 版本 | 链接方式 |
|---|---|---|
| libzmq | ≥ 4.3 | 静态（设备）/ 动态（主机） |
| OpenSSL | ≥ 3.0 | 静态（设备）/ 动态（主机） |
| yaml-cpp | ≥ 0.8 | 静态 |
| spdlog | ≥ 1.10 | 静态（设备）/ 动态（主机） |
| fmt | ≥ 9.0 | header-only |
| protobuf-lite | v3.x | 静态 |

apt 一行：`cmake libspdlog-dev libgtest-dev libbenchmark-dev libyaml-cpp-dev libzmq3-dev libssl-dev protobuf-compiler libprotobuf-lite32t64`

---

## 命名约定（CLAUDE.md §2）

| 对象 | 约定 |
|---|---|
| 命名空间 | `udaf::` + `udaf::ability_a/b/c::` + `udaf::core::` + `udaf::platform::fs::` |
| 类 | PascalCase |
| 函数 | snake_case |
| 成员变量 | snake_case_（尾下划线） |
| C++ 头文件 | `.hpp` / C++ 实现 `.cpp` |
| C 接口 | `.h` + `.c` |

---

## 关键约束（CLAUDE.md §3）

1. **严禁明文传输用户名密码**（业务硬约束）
2. **greenfield 不兼容 ref/**（无 df_* / MC_* / MsgHeader DFRM magic）
3. **禁止异常 / 裸 new/delete / `using namespace`**
4. **不引入 dora-rs Rust 运行时**
5. **跨主机节点调度必须白名单**（设备端防恶意调度）
6. **定期发现频率限制**（防 O(N²) 广播风暴）

---

## 项目流程

UDAF 五阶段瀑布流程（CLAUDE.md §4）：

```
阶段 1：需求设计      → docs/01-requirements.md
阶段 2：架构设计      → docs/02-architecture.md
阶段 3：概要设计      → docs/03-detailed-design.md
阶段 4：详细设计      → docs/04-module-design.md
阶段 5：测试方案      → docs/05-test-plan.md
实现阶段（设计完成后）→ udaf/src/
```

每阶段需通过用户评审 + 验收清单勾选 + 用户明确同意，方可进入下一阶段。

---

## License

MIT — 详见 [`LICENSE`](LICENSE)。

## 版本

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.3.13 | 2026-09-01 | 4 项 PSK 握手校验测试 + 3 项集成测试 + 4 项性能契约 / 412 测试全绿 / 函数覆盖率首破 95% / 设计文档 4 份同步升至 v2.9/v2.2/v0.8/v0.8 |
| v0.3.12 | 2026-08-31 | 修复 udaf_client_trust_list 堆释放后使用（ASan）/ 406 测试全绿 / ASan bug 0 |
| v0.3.11 | 2026-08-31 | 11 模块全部实现 / 406 测试通过 / 94.0% lines 覆盖 / 29 项性能契约 / deb 打包 / CI 8 job / C SDK |
| v0.2.0 | 2026-08-30 | 11 模块全部实现 / 252 测试通过 / 85.8% lines 覆盖 / 25 项性能契约 / deb 打包 / CI 8 job / C SDK |
| v0.1.0 | 2026-08-28 | 仓库骨架 + CMake + CI 占位 + 设计文档 v1.0 |