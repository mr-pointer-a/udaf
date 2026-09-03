# 05 测试方案

> **项目代号**：UDAF（Unified Device & Application Framework，统一设备与应用框架）
> **文档版本**：v0.9
> **日期**：2026-09-01
> **阶段**：阶段 5 / 5（测试方案）
> **前置文档**：[`docs/01-requirements.md`](01-requirements.md) v1.0、[`docs/02-architecture.md`](02-architecture.md) v2.10、[`docs/03-detailed-design.md`](03-detailed-design.md) v2.3、[`docs/04-module-design.md`](04-module-design.md) v0.9
> **状态**：草案

---

## 1. 背景

UDAF 项目经历五个设计阶段（需求→架构→概要→详细→测试），本阶段输出测试方案，为实现阶段提供完整的质量保障框架。

**历史材料教训**（`ref/` 项目）：
- `ref/device_framework`：协议 + 错误码测试覆盖较好，但 fork/daemon/watchdog 缺集成测试
- `ref/msgc`：测试未集成 CTest，每个模块独立测试程序，无统一入口
- `ref/rootfs_app`：RPC 代码生成器缺单元测试

**UDAF 改进方向**：
- 统一 GoogleTest 框架 + CTest 集成
- 每个公共 API 至少 3 个测试用例
- 33 项性能基准测试
- 模糊测试覆盖协议编解码

---

## 2. 现状痛点

### 2.1 ref/ 测试现状

| 项目 | 痛点 | 影响 |
|------|------|------|
| device_framework | fork/daemon/watchdog 缺集成测试 | 生产环境崩溃恢复未验证 |
| msgc | 测试未集成 CTest，独立测试程序 | 无统一入口，CI 集成困难 |
| rootfs_app | RPC 代码生成器缺单元测试 | 代码质量不可控 |
| 三套共通 | 没有统一 CI，无自动化测试 | 回归风险高 |

### 2.2 UDAF 测试改进

| 维度 | ref/ 现状 | UDAF 目标 |
|------|----------|----------|
| 框架 | 各自选择 | 统一 GoogleTest + Google Benchmark |
| CI | 无 | GitHub Actions / GitLab CI |
| 覆盖率 | 无度量 | 核心 > 90%，其他 > 80% |
| 性能 | 无基准 | 33 项性能基准 |
| 模糊测试 | 无 | libFuzzer 覆盖协议/加密 |

---

## 3. 目标

| 目标 | 指标 | 出处 |
|------|------|------|
| 核心模块单元测试覆盖率 | > 90% | 01 §5.4 |
| 其他模块单元测试覆盖率 | > 80% | 01 §5.4 |
| 集成测试关键路径覆盖率 | 100% | 01 §5.4 |
| 性能基准测试 | 33 项全部通过 | 02 §3.4 |
| 静态分析 | 全部通过 | 01 §5.4 |

---

## 4. 通过标准

### 4.1 测试通过标准

| 类型 | 通过条件 | 阻断级别 |
|------|---------|---------|
| 单元测试 | 全部通过 | **阻断合并** |
| 集成测试 | 全部通过 | **阻断合并** |
| 性能基准 | 全部 ≤ 目标值 | **阻断发版** |
| 模糊测试 | 无 crash / 无 ASAN 报告 | **阻断合并** |
| 压力测试 | 无内存泄漏、无死锁 | **阻断发版** |
| 静态分析 | 0 error | **阻断合并** |

### 4.2 覆盖率目标

#### 4.2.1 模块级覆盖率

| 模块 | 目标覆盖率 | 说明 |
|------|-----------|------|
| udaf::core | > 90% | 核心基础库 |
| udaf::ability_a::* | > 90% | 能力 A |
| udaf::ability_b::* | > 90% | 能力 B |
| udaf::ability_c::* | > 85% | 能力 C（含子进程交互） |
| udaf::bridge | > 90% | 跨能力桥接 |
| udaf::cli | > 80% | CLI 工具 |
| udaf::audit | > 90% | 审计日志 |
| udaf::sdk | > 85% | C API 封装 |
| udaf::observability | > 80% | 可观测性 |

#### 4.2.2 指标级覆盖率（精细化目标）

| 模块等级 | 行覆盖率 | 分支覆盖率 | 函数覆盖率 |
|----------|---------|-----------|-----------|
| **核心模块**（core/ability_a/ability_b/bridge/audit/crypto/platform） | ≥ 90% | ≥ 80% | ≥ 95% |
| **非核心模块**（cli/observability/sdk/ability_c） | ≥ 80% | ≥ 70% | ≥ 85% |
| **公共 API**（所有模块对外接口） | 100% | ≥ 90% | 100% |

#### 4.2.3 模糊测试专属覆盖率

| 指标 | 目标 | 说明 |
|------|------|------|
| corpus 文件数 | ≥ 100 个/target | 累积语料库 |
| 边覆盖率（edge coverage） | 增长率 ≤ 1 new edge / 10000 次迭代 | 视为收敛 |
| ASAN/MSAN/UBSAN | 0 报告 | 阻断合并 |

#### 4.2.4 覆盖率工具链

- **lcov**：≥ 1.16，固定 lcov 2.x 兼容版本
- **配置**：`--rc lcov_branch_coverage=1` 启用分支覆盖率
- **生成**：`genhtml --branch-coverage` 输出分支覆盖报告
- **排除目录**：`/usr/*`、`*/tests/*`、`*/build/*`

---

## 5. 详细设计

### 5.1 测试分类与框架

| 类型 | 框架 | 覆盖目标 | 执行频率 | 出处 |
|------|------|----------|----------|------|
| 单元测试 | GoogleTest | 每个公共 API 至少 3 用例（正面/负面/边界） | 每次提交 | 03 §13.1 |
| 集成测试 | GoogleTest + subprocess | 跨模块链路（§4.4 中 5 条链路） | 每次提交 | 03 §10 |
| 性能基准 | Google Benchmark | §4.5 的 33 项性能契约 | 每日 | 02 §3.4 |
| 模糊测试 | libFuzzer | 协议编解码、加密握手 | 每周 | 03 §13.1 |
| 压力测试 | 自定义 | 10000 注册、64 sequence 滑动窗口 | 发版前 | 03 §13.1 |

### 5.2 测试命名规范

> 全文统一 GoogleTest snake_case 命名风格（与03 §13.2 一致）。

| 类型 | 命名格式 | 示例 |
|------|---------|------|
| 单元测试 | `test_<class>_<method>_<scenario>` | `test_service_registry_upsert_and_query` |
| 集成测试 | `test_<integration_scenario>` | `test_discovery_to_topology_chain` |
| 性能基准 | `udaf_bench <scenario>` | `udaf_bench channel_throughput` |
| 负面断言 | `test_<class>_<method>_<expected_error>` | `test_serializer_schema_version_mismatch` |
| 模糊测试 | `fuzz_<target>` | `fuzz_protocol_decode` |
| 压力测试 | `stress_<scenario>` | `stress_registry_10000` |

**错误码引用**：负面断言中的错误码必须为 ADR-011 中存在的枚举名（如 `CRYPTO_HMAC_MISMATCH`）。

#### 5.3.0 安全测试矩阵

> 对齐 CLAUDE.md §3 关键约束 #1（严禁明文传输用户名密码）+ #7（跨主机节点调度必须白名单）。

| 测试文件 | 补充用例 | 验证内容 |
|---------|---------|---------|
| `test_hmac.cc` | `constant_time_compare` | 常量时间比较验证 timing attack 防护 |
| `test_hmac.cc` | `verify_invalid_key_length` | 错误密钥长度返回 `CRYPTO_HMAC_MISMATCH` |
| `test_tls.cc` | `reject_self_signed` | 拒绝自签名证书 |
| `test_tls.cc` | `reject_untrusted_ca` | 拒绝不受信任 CA |
| `test_tls.cc` | `reject_wrong_san` | CN/SAN 不匹配拒绝 |
| `test_tls.cc` | `reject_wrong_key_usage` | 密钥用途不匹配拒绝 |
| `test_authenticator.cc` | `psk_brute_force_protection` | 连续错误尝试达到阈值后锁定 |
| `test_crypto.cc` | `kdf_salt_uniqueness` | KDF salt 唯一性验证 |
| `test_crypto.cc` | `kdf_min_iterations` | KDF 迭代次数 ≥ 100000 |
| `test_crypto.cc` | `encrypt_nonce_reuse_detection` | nonce 重用攻击检测 |
| `test_advertiser.cc` | `replay_old_sequence` | 旧 sequence 重放拒绝 |
| `test_advertiser.cc` | `replay_future_sequence` | 未来时间戳重放拒绝 |
| `test_advertiser.cc` | `sequence_window_overflow` | 64 sequence 滑动窗口溢出拒绝 |
| 集成测试 | `test_mitm_attack_blocked` | MITM 攻击阻断（伪造 CA/降级） |
| `test_authenticator.cc` | `rate_limit` | 认证失败速率限制 |
| `test_authenticator.cc` | `clock_skew_tolerance` | 时钟偏差 ±30s 容差 |
| `test_message_handler_validation.cc` | `special_chars_null_byte` | 特殊字符/NULL 字节拒绝 |
| `test_message_handler_validation.cc` | `unicode_normalization_attack` | Unicode 规范化攻击 |
| `test_message_handler_validation.cc` | `control_char_injection` | 控制字符注入 |
| `test_config.cc` | `yaml_billion_laughs_protection` | YAML billion laughs 防护 |
| `test_config.cc` | `yaml_recursive_anchor_protection` | YAML 递归锚点防护 |
| `test_file_xfer_node.cc` | `path_traversal_blocked` | `../` 绝对路径/符号链接穿越拒绝 |
| `test_cmd_exec_node.cc` | `command_injection_blocked` | `;`/`&&`/反引号/`$()` 注入拒绝 |
| `test_scheduler.cc` | `whitelist_bypass_variants` | 大小写/URL编码/Unicode 规范化绕过测试 |
| 集成测试 | `test_cross_host_scheduling_permission` | 子网限制/IP 伪造/MAC 绑定 |
| 集成测试 | `test_device_rejects_malicious_schedule` | 设备端拒绝非白名单主机调度（CLAUDE.md §3 #7） |
| 集成测试 | `test_privilege_escalation_blocked` | 伪造身份调度拒绝 |
| 集成测试 | `test_bidirectional_whitelist_consistency` | A↔B 双向白名单一致性 |
| `test_audit_logger.cc` | `log_immutability` | HMAC 链验证/篡改检测 |
| `test_audit_logger.cc` | `password_redacted` | 审计日志密码脱敏 |
| `test_audit_logger.cc` | `access_control` | 审计日志访问控制 |

#### 5.3.0.1 安全模糊测试目标（新增）

| 目标 | 测试文件 | 输入 | 预期 |
|------|---------|------|------|
| 重放攻击 | `fuzz_security_replay_attack` | 随机 sequence + timestamp | 无重放接受、graceful reject |
| 恶意节点 | `fuzz_security_malicious_node` | 伪造 node_id + 非法 payload | 不进入注册表、返回 BIZ_AUTH_UNTRUSTED |
| PSK 暴力破解 | `stress_security_psk_bruteforce` | 1000 错误 PSK/秒 | 5 次后锁定 |

#### 5.3.0 错误码测试矩阵

> 错误码定义遵循 ADR-011（`SCREAMING_SNAKE_CASE`）。负面断言须显式调用 `EXPECT_EQ(result.err(), ErrorCode::XXX)` 验证。

| 错误码 | 出现场景 | 触发的测试模块 | 必含断言 |
|--------|---------|--------------|---------|
| `INVALID_ARG` | 参数非法 | `test_config`, `test_parameter_handler_validation` | `EXPECT_EQ(ec, INVALID_ARG)` |
| `CONFIG_PARSE_FAILED` | YAML 解析失败 | `test_config` | `EXPECT_EQ(ec, CONFIG_PARSE_FAILED)` |
| `CONFIG_MISSING_REQUIRED` | 必填字段缺失 | `test_config` | `EXPECT_EQ(ec, CONFIG_MISSING_REQUIRED)` |
| `PROTOCOL_INVALID_MAGIC` | 魔数错误 | `test_protocol_decode` | `EXPECT_EQ(ec, PROTOCOL_INVALID_MAGIC)` |
| `PROTOCOL_PAYLOAD_TOO_LARGE` | payload 超 MTU | `test_protocol_decode` | `EXPECT_EQ(ec, PROTOCOL_PAYLOAD_TOO_LARGE)` |
| `PROTOCOL_VERSION_MISMATCH` | 协议版本不一致 | `test_protocol_decode` | `EXPECT_EQ(ec, PROTOCOL_VERSION_MISMATCH)` |
| `NET_TIMEOUT` | 网络超时 | `test_tcp_channel`, `test_scanner`, `test_cmd_exec_node` | `EXPECT_EQ(ec, NET_TIMEOUT)` |
| `NET_CONNECTION_REFUSED` | 连接被拒 | `test_tcp_channel` | `EXPECT_EQ(ec, NET_CONNECTION_REFUSED)` |
| `CRYPTO_HMAC_MISMATCH` | HMAC 校验失败 | `test_hmac`, `test_crypto` | `EXPECT_EQ(ec, CRYPTO_HMAC_MISMATCH)` |
| `CRYPTO_PSK_MISMATCH` | PSK 不匹配 | `test_authenticator` | `EXPECT_EQ(ec, CRYPTO_PSK_MISMATCH)` |
| `CRYPTO_CERT_EXPIRED` | 证书过期 | `test_authenticator`, `test_tls` | `EXPECT_EQ(ec, CRYPTO_CERT_EXPIRED)` |
| `BIZ_SERVICE_NOT_FOUND` | 服务未找到 | `test_service_registry`, `test_cli_discovery` | `EXPECT_EQ(ec, BIZ_SERVICE_NOT_FOUND)` |
| `BIZ_NODE_NOT_REGISTERED` | 节点未注册 | `test_scheduler` | `EXPECT_EQ(ec, BIZ_NODE_NOT_REGISTERED)` |
| `RES_MEMORY_EXHAUSTED` | 内存耗尽 | 集成测试 / 压力测试 | `EXPECT_EQ(ec, RES_MEMORY_EXHAUSTED)` |
| `INTERNAL` | 内部错误 | 通用 | `EXPECT_EQ(ec, INTERNAL)` |
| `NOT_IMPLEMENTED` | 未实现 | 通用 | `EXPECT_EQ(ec, NOT_IMPLEMENTED)` |

**测试覆盖要求**：
- 单元测试：每个错误码至少 1 个显式断言用例
- 集成测试：链路失败时返回的错误码必须与本表一致
- 模糊测试：注入错误输入必须返回合法错误码，不得崩溃

### 5.3 单元测试策略

#### 5.3.1 测试文件清单

按模块划分，每个测试文件包含 3-7 个测试用例。

##### udaf::core（8 个测试文件，~38 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_result.cc` | Result\<T\> | ok/value移动构造/err/void/ignore/折叠 | 7 |
| `test_error_code.cc` | ErrorCode | i18n消息/CLI退出码/类别分类/范围检查 | 4 |
| `test_config.cc` | Config | 默认值/YAML加载/环境变量覆盖/缺失字段/无效值 | 5 |
| `test_serializer.cc` | SerializerBase + Serializer\<T\> | 编码/解码/往返/Schema版本/最大负载/roundtrip | 6 |
| `test_channel_base.cc` | Channel | 生命周期/health/类型抽象 | 3 |
| `test_inproc_channel.cc` | InprocChannel | send/recv/health/双向通信 | 4 |
| `test_logger.cc` | Logger + Log rotation | 输出格式/级别过滤/轮转 | 3 |
| `test_wal.cc` | Wal | 写入/回放/截断/恢复/最大条目/无效路径 | 6 |

##### udaf::ability_a::discovery（3 个测试文件，~16 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_advertiser.cc` | Advertiser | 构建消息/start/stop/重放防护/并发安全 | 5 |
| `test_scanner.cc` | Scanner | 解析响应/start/stop/超时/降级 | 5 |
| `test_service_registry.cc` | ServiceRegistry | upsert/query/remove/cleanup_expired/批量/并发 | 6 |

##### udaf::ability_a::network（2 个测试文件，~8 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_message_handler.cc` | MessageHandler | 构建请求/构建响应/解析/roundtrip | 4 |
| `test_message_handler_validation.cc` | MessageHandler | 校验设备信息/网络参数/错误码/空字段 | 4 |

##### udaf::ability_a::parameters（2 个测试文件，~7 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_parameter_handler.cc` | ParameterHandler | get/set/roundtrip/persistence | 4 |
| `test_parameter_handler_validation.cc` | ParameterHandler | 无效字段/空值/未知字段 | 3 |

##### udaf::ability_a::encryption（1 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_crypto.cc` | Crypto | derive_key/encrypt_decrypt/invalid_key/wrong_algorithm/reuse_nonce/roundtrip | 6 |

##### udaf::crypto（5 个测试文件，~18 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_hmac.cc` | Hmac | compute/verify/invalid_key/wrong_length | 4 |
| `test_tls.cc` | TlsContext | handshake/encrypt_decrypt/cert_validation | 3 |
| `test_pki.cc` | PkiManager | sign/verify/revoke/cert_chain | 4 |
| `test_keystore.cc` | Keystore | load/save/get_key/list_keys | 4 |
| `test_authenticator.cc` | Authenticator | authenticate_psk/authenticate_pki/reject_expired | 3 |

##### udaf::platform（3 个测试文件，~10 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_unique_fd.cc` | UniqueFd | 析构关闭/移动语义/reset/EINTR | 4 |
| `test_fork_thread.cc` | ForkThread | submit/wait/timeout/kill | 4 |
| `test_daemonize.cc` | daemonize | 后台化/信号处理/pid文件 | 2 |

##### udaf::ability_b::topology（1 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_topology.cc` | Topology | add_node/connect/disconnect/all_nodes/all_edges/has_cycle | 6 |

##### udaf::ability_b::scheduler（1 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_scheduler.cc` | Scheduler | start_stop/restart_backoff/whitelist/block_expired/broadcast | 6 |

##### udaf::ability_b::channel（2 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_ipc_channel.cc` | IpcChannel | connect/send/recv | 3 |
| `test_tcp_channel.cc` | TcpChannel | connect/send/recv_encrypted | 3 |

##### udaf::ability_c（4 个测试文件，~19 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_cmd_exec_node.cc` | CmdExecNode | 启动/执行/超时/输出捕获/resource_limits/错误处理 | 6 |
| `test_file_xfer_node.cc` | FileXferNode | upload/download/chunk/integrity | 4 |
| `test_heartbeat_node.cc` | HeartbeatNode | 发送/接收/timeout/资源采集 | 4 |
| `test_net_info_node.cc` | NetInfoNode | 采集/set/get | 3 |

##### udaf::bridge（2 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_discovery_bridge.cc` | DiscoveryBridge | 事件分发/回调触发/多事件 | 3 |
| `test_topology_update_callbacks.cc` | TopologyUpdateCallbacks | 节点上下线/批量更新 | 3 |

##### udaf::cli（2 个测试文件，~6 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_cli_common.cc` | cli::common | parse_args/format_output | 3 |
| `test_cli_discovery.cc` | cli::discovery | discover/list/register | 3 |

##### udaf::audit（2 个测试文件，~7 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_audit_logger.cc` | AuditLogger | write/rotate/query/write_concurrent | 4 |
| `test_audit_log_entry.cc` | AuditLogEntry | 构建/序列化/反序列化 | 3 |

##### udaf::sdk（1 个测试文件，~3 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_udaf_c_api.cc` | UDAF C API | init/discover/destroy | 3 |

##### udaf::observability（1 个测试文件，~3 用例）

| 测试文件 | 被测类 | 测试用例 | 用例数 |
|---------|--------|---------|--------|
| `test_metrics_collector.cc` | MetricsCollector | record/aggregate/lifecycle | 3 |

**总计**：~38 个测试文件，~150+ 个测试用例

#### 5.3.1.1 边界与并发补充用例

> Round 3 评审新增：覆盖空输入、最大长度精确边界、并发竞态、错误恢复场景。

| 测试文件 | 补充用例 | 验证内容 |
|---------|---------|---------|
| `test_serializer` | `decode_empty_buffer` | 0 字节缓冲区返回 SERIALIZE_DECODE_FAILED |
| `test_serializer` | `decode_payload_at_max` | payload = kMaxPayloadSize 成功 |
| `test_serializer` | `decode_payload_over_max` | payload = kMaxPayloadSize+1 返回 PROTOCOL_PAYLOAD_TOO_LARGE |
| `test_config` | `parse_empty_yaml` | 空 YAML 文件返回 CONFIG_PARSE_FAILED |
| `test_config` | `parse_null_value` | 字段值为 null 时的处理 |
| `test_service_registry` | `upsert_empty_node_id` | 空 node_id 拒绝 |
| `test_service_registry` | `concurrent_upsert_same_id` | 8 线程并发 upsert 同一 node_id 无数据竞争 |
| `test_ipc_channel` | `recv_timeout_empty_queue` | 空队列 recv 返回 RecvStatus::TIMEOUT |
| `test_tcp_channel` | `connect_timeout_unreachable` | TCP 连接不存在时返回 NET_TIMEOUT |
| `test_wal` | `recover_truncated_file` | 截断 WAL 文件恢复行为 |
| `test_wal` | `write_disk_full` | 磁盘满时的错误处理 |
| `test_wal` | `recover_partial_write` | 写入中途 kill 的部分数据一致性 |
| `test_topology` | `concurrent_add_remove` | 多线程并发 add_node/remove_node 无竞态 |
| `test_scheduler` | `concurrent_start_stop` | 多线程并发 start/stop 线程安全 |

#### 5.3.1.2 重试/降级/故障转移测试

| 测试文件 | 补充用例 | 验证内容 |
|---------|---------|---------|
| `test_scanner` | `retry_after_timeout` | 超时后重试扫描 |
| `test_tcp_channel` | `reconnect_after_disconnect` | TCP 断开后自动重连 |
| `test_heartbeat_node` | `retry_after_no_ack` | 心跳无应答后重试 + 指数退避 |
| 集成测试 | `failover_to_backup` | 主节点故障后切换到备用节点 |
| 集成测试 | `degrade_on_partial_failure` | 部分节点故障时降级为只读模式 |

#### 5.3.2 测试夹具设计

```cpp
// 通用测试夹具
class UdafTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时目录用于 WAL、日志、配置
        temp_dir_ = std::filesystem::temp_directory_path() / "udaf_test";
        std::filesystem::create_directories(temp_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    std::filesystem::path temp_dir_;
};

// 能力 A 测试夹具
class AbilityATest : public UdafTest {
protected:
    void SetUp() override {
        UdafTest::SetUp();
        registry_ = std::make_unique<udaf::ability_a::registry::ServiceRegistry>();
    }
    void TearDown() override {
        registry_->cleanup_expired(std::chrono::seconds{0});
        registry_.reset();
        UdafTest::TearDown();
    }
    std::unique_ptr<udaf::ability_a::registry::ServiceRegistry> registry_;
};

// 能力 B 测试夹具
class AbilityBTest : public UdafTest {
protected:
    void SetUp() override {
        UdafTest::SetUp();
        wal_ = std::make_unique<udaf::core::Wal>(temp_dir_ / "test.wal");
    }
    void TearDown() override {
        wal_.reset();
        UdafTest::TearDown();
    }
    std::unique_ptr<udaf::core::Wal> wal_;
};

// 能力 C 测试夹具（含进程管理）
class AbilityCTest : public AbilityBTest {
protected:
    void SetUp() override {
        AbilityBTest::SetUp();
        // 设置资源限制（与生产环境一致）
        set_resource_limits();
    }
    void TearDown() override {
        AbilityBTest::TearDown();
    }
};
```

### 5.4 集成测试策略

#### 5.4.1 跨模块集成链路

| # | 链路名称 | 测试文件 | 验证内容 | 出处 |
|---|---------|---------|---------|------|
| 1 | A→B 拓扑更新 | `test_a_to_b_topology.cc` | 新设备加入 → 注册表更新 → 200ms debounce → 30s 稳定性窗口 → 双重白名单 → 拓扑新增节点 | 03 §10 链路 1 |
| 2 | B→C 节点调度 | `test_b_to_c_scheduling.cc` | 节点启动 → spawn 前白名单二次校验 → fork+exec → 资源隔离 → 崩溃恢复 | 03 §10 链路 2 |
| 3 | C→A 服务查询 | `test_c_to_a_query.cc` | C 启动 → 查询 A 注册表 → self_host 白名单校验 → SubscriptionHandle RAII → 回调触发 → 建立连接 | 03 §10 链路 3 |
| 4 | A→Crypto 加密发现 | `test_a_to_crypto.cc` | 发现消息加密 → 对端解密 → PSK 身份验证 → 重放防护 | 03 §10 链路 4 |
| 5 | B+WAL 崩溃恢复 | `test_b_wal_recovery.cc` | 写入 WAL → kill -9 → 重启 → 回放恢复 → 白名单重新校验 | 03 §10 链路 5 |

#### 5.4.2 集成测试环境（Round 4 增强）

```cpp
// 集成测试夹具（真实网络 + 真实进程 + 真实端口分配）
class IntegrationTest : public UdafTest {
protected:
    void SetUp() override {
        UdafTest::SetUp();

        // 1. 端口分配（避免冲突，分配 19000-19999 端口池）
        udaf_port_a_ = get_ephemeral_port(19000, 19999);
        udaf_port_b_ = get_ephemeral_port(19000, 19999);
        udaf_port_c_ = get_ephemeral_port(19000, 19999);

        // 2. UDP 广播组（链路 1/4 使用）
        multicast_group_ = "239.255.42.99";

        // 3. 启动完整 UDAF 栈（A + B + C）
        // 使用 subprocess 启动 daemon
        host_process_ = subprocess::launch(
            "build/udafd",
            {"--config", (temp_dir_ / "udaf_host.yaml").string(),
             "--port", std::to_string(udaf_port_a_),
             "--multicast", multicast_group_}
        );

        device_process_ = subprocess::launch(
            "build/udafd-device",
            {"--config", (temp_dir_ / "udaf_device.yaml").string(),
             "--port", std::to_string(udaf_port_c_)}
        );

        // 4. 等待就绪信号（端口监听 + 进程健康）
        wait_for_port_ready(udaf_port_a_, std::chrono::seconds{5});
        wait_for_port_ready(udaf_port_c_, std::chrono::seconds{5});

        // 5. 初始化客户端连接
        client_ = std::make_unique<udaf::sdk::Client>("localhost", udaf_port_a_);
    }

    void TearDown() override {
        client_.reset();
        subprocess::terminate(host_process_);
        subprocess::terminate(device_process_);
        UdafTest::TearDown();
    }

    // 端口分配辅助
    static uint16_t get_ephemeral_port(uint16_t min, uint16_t max) {
        for (uint16_t p = min; p <= max; ++p) {
            if (is_port_available(p)) return p;
        }
        throw std::runtime_error("no available port");
    }

    // Mock 严格限制：集成测试不得使用 Mock 网络/进程
    // 仅允许对外部依赖（如硬件驱动）使用 Mock

    int udaf_port_a_;
    int udaf_port_b_;
    int udaf_port_c_;
    std::string multicast_group_;
    subprocess::Process host_process_;
    subprocess::Process device_process_;
    std::unique_ptr<udaf::sdk::Client> client_;
};
```

#### 5.4.3 集成测试断言规范（Round 4 新增）

每条集成链路必须包含显式断言点，对齐 §5.3.0 错误码矩阵：

| 链路 | 必含断言 |
|------|---------|
| 链路 1：A→B 拓扑更新 | `EXPECT_EQ(registry.size(), kExpectedNodeCount);`<br>`ASSERT_TRUE(callback_invoked_count == 1);`<br>`EXPECT_EQ(topology.node_count(), 1);` |
| 链路 2：B→C 节点调度 | `ASSERT_TRUE(spawned_pid_alive);`<br>`EXPECT_EQ(scheduler.state(), kRunning);`<br>`EXPECT_EQ(ec, OK) on success;` |
| 链路 3：C→A 服务查询 | `ASSERT_TRUE(connection_established);`<br>`EXPECT_GT(handshake_latency_ms, 0);`<br>`EXPECT_EQ(payload.node_id, kExpectedNodeId);` |
| 链路 4：A→Crypto 加密发现 | `EXPECT_EQ(ec, OK) on valid PSK;`<br>`EXPECT_EQ(ec, CRYPTO_PSK_MISMATCH) on invalid;`<br>`EXPECT_EQ(replay_detected, true);` |
| 链路 5：B+WAL 崩溃恢复 | `EXPECT_EQ(topology_hash_before, topology_hash_after);`<br>`EXPECT_EQ(registry_size_after, registry_size_before);` |

#### 5.4.4 失败注入矩阵（Round 4 新增）

| 链路 | 故障注入场景 | 期望行为 |
|------|------------|---------|
| 链路 1 | 广播丢包 30% | 拓扑仍最终一致（200ms debounce + 重传） |
| 链路 1 | 白名单拒绝 | 节点不上线、回调触发 `on_node_rejected` |
| 链路 2 | fork 失败（ulimit） | 返回 `RES_FD_EXHAUSTED`、不污染拓扑 |
| 链路 2 | exec 权限不足 | 返回 `BIZ_CMD_EXEC_FAILED`、scheduler 重试 |
| 链路 3 | 连接超时 | 返回 `NET_TIMEOUT`、客户端重连 |
| 链路 3 | 回调异常 | 上层捕获、不影响其他订阅者 |
| 链路 4 | 重放攻击 | sequence 滑动窗口拒绝、返回 `PROTOCOL_PROTOCOL_REJECTED` |
| 链路 4 | PSK 不匹配 | 返回 `CRYPTO_PSK_MISMATCH`、不进入注册表 |
| 链路 5 | 磁盘满 | WAL 写入失败、返回 `RES_DISK_FULL`、回放正常 |
| 链路 5 | WAL 损坏 | 检测截断、返回 `SERIALIZE_VERSION_MISMATCH`、不崩溃 |

#### 5.4.5 数据一致性验证（Round 4 新增）

每条链路必须补充载荷完整性断言：

```cpp
// 示例：链路 4 加密发现的载荷完整性验证
TEST_F(IntegrationTest, A_to_Crypto_PayloadIntegrity) {
    // 构造发送方载荷
    auto send_payload = udaf::ability_a::registry::RegistryEntry{
        .node_id_ = "test-device-001",
        .bind_address_ = "192.168.1.100",
        .bind_port_ = 5000,
        .services_ = {},
    };

    // 发送加密广播
    broadcaster_->send_encrypted(send_payload);

    // 等待对端接收
    auto recv_payload = wait_for_received_payload(std::chrono::seconds{5});

    // 逐字段比对
    EXPECT_EQ(recv_payload.node_id_, send_payload.node_id_);
    EXPECT_EQ(recv_payload.bind_address_, send_payload.bind_address_);
    EXPECT_EQ(recv_payload.bind_port_, send_payload.bind_port_);
}
```

#### 5.4.6 能力 C 端到端集成链路（Round 4 新增）

| 链路 | 测试文件 | 验证内容 |
|------|---------|---------|
| C1：文件传输 E2E | `test_c_e2e_file_xfer.cc` | PC → 设备 文件分块上传 → 完整性校验 → 接收确认 |
| C2：命令执行 E2E | `test_c_e2e_cmd_exec.cc` | PC 发送命令 → 设备 fork+exec → 输出捕获 → 资源限制 |
| C3：心跳采集 E2E | `test_c_e2e_heartbeat.cc` | 设备定时心跳 → PC 聚合 → 超时检测 → 状态切换 |
| C4：网络信息 E2E | `test_c_e2e_net_info.cc` | 设备采集网络参数 → PC 接收 → set/get 验证 |

### 5.5 性能基准测试策略

#### 5.5.1 基准测试清单

对齐02 §3.4 的 33 项性能契约：

| # | 基准名 | 测量目标 | 通过条件 | 出处 |
|---|--------|---------|---------|------|
| 1 | `udaf_bench command_roundtrip` | 命令往返延迟 P95 | < 5ms | 01 §5.1 |
| 2 | `udaf_bench command_roundtrip_p99` | 命令往返延迟 P99 | < 15ms | 01 §5.7 |
| 3 | `udaf_bench remote_command_roundtrip` | 跨子网延迟 P95 | < 200ms | 01 §5.1 |
| 4 | `udaf_bench heartbeat_aggregation_100` | 100 设备心跳聚合 P95 | < 10ms | 01 §5.1 |
| 5 | `udaf_bench file_transfer_throughput` | 文件传输吞吐 | > 80 MB/s | 01 §5.1 |
| 6 | `udaf_bench device_memory_idle` | 设备端空闲内存 | < 8MB | 01 §5.1 |
| 7 | `udaf_bench host_memory_idle` | 主机端空闲内存 | < 32MB | 01 §5.1 |
| 8 | `udaf_bench device_startup` | 设备端启动时间 | < 200ms | 01 §5.1 |
| 9 | `udaf_bench node_startup` | B 节点启动时间 | < 50ms | 01 §5.1 |
| 10 | `udaf_bench c_node_startup` | C 节点启动时间 | ≤ 50ms | 01 §5.1 |
| 11 | `udaf_bench inproc_latency` | 同主机消息延迟 P95 | < 100μs | 01 §5.1 |
| 12 | `udaf_bench crypto_overhead` | 加密性能开销 | 吞吐损失 < 20% | 01 §5.1 |
| 13 | `udaf_bench middleware_throughput_ipc` | 同主机消息吞吐 | ≥ 50K msg/s | 01 §5.1 |
| 14 | `udaf_bench middleware_throughput_tcp` | 跨主机消息吞吐 | ≥ 5K msg/s | 01 §5.1 |
| 15 | `udaf_bench max_concurrent_nodes` | 最大并发节点数 | ≥ 1000 | 01 §5.1 |
| 16 | `udaf_bench registry_capacity` | 服务注册表容量 | ≥ 10000 | 01 §5.1 |
| 17 | `udaf_bench observability_overhead` | 可观测性自身开销 | CPU < 5%, 内存 < 2% | 01 §5.1 |
| 18 | `udaf_bench fork_latency` | fork-only 延迟 | < 3ms | ADR-003 §5.5 |
| 19 | `udaf_bench fork_exec_latency` | fork+exec 延迟 | ≤ 80ms | ADR-003 §5.5 |
| 20 | `udaf_bench psk_handshake` | PSK 握手延迟 P95 | < 2ms | 01 §5.7 |
| 21 | `udaf_bench audit_write_throughput` | 审计日志写入吞吐 | ≥ 1000 条/秒 | 01 §5.7 |
| 22 | `udaf_bench incremental_build` | 增量构建时间 | < 30s | 01 §5.1 |
| 23 | `udaf_bench device_peak_memory` | 设备端峰值内存 | < 16MB | 01 §5.7 |
| 24 | `udaf_bench host_peak_memory` | 主机端峰值内存 | < 128MB | 01 §5.7 |
| 25 | `udaf_bench crash_recovery` | 设备端崩溃恢复时延 | ≤ 5s | 01 §7.6 |
| 26 | `udaf_bench tcp_latency_p99` | 跨主机消息延迟 P99 | < 15ms | 01 §5.7 |
| 27 | `udaf_bench pki_handshake` | PKI 握手延迟 P95 | < 50ms | 01 §5.7 |
| 28 | `udaf_bench cpu_idle` | 设备端 CPU 占用（空闲） | < 5% | 01 §5.1 |
| 29 | `udaf_bench soak` | 长期内存稳定性 30 天 | RSS 增长 < 10% | 01 §5.1 |
| 30 | `udaf_bench aead_throughput_1mb` | AEAD 大块吞吐 | ≥ 200 MB/s | v0.3.13（02 v2.9） |
| 31 | `udaf_bench audit_verify_chain` | 审计 hash chain 全链校验 | ≤ 100ms / 500 条 | v0.3.13（02 v2.9） |
| 32 | `udaf_bench wal_replay_full` | WAL append+replay 完整链路 | ≤ 50ms / 200 条 | v0.3.13（02 v2.9） |
| 33 | `udaf_bench topology_commit_50` | 拓扑事务批量 commit | ≤ 100ms / 50 节点 | v0.3.13（02 v2.9） |

#### 5.5.2 基准测试实现

```cpp
// 示例：命令往返延迟基准（snake_case 函数名，符合 CLAUDE.md §2）
static void benchmark_command_roundtrip(benchmark::State& state) {
    for (auto _ : state) {
        // 发送命令 → 等待响应
        auto result = channel_->send_and_recv(test_payload);
        benchmark::DoNotOptimize(result);
    }
}
// Google Benchmark 参数：min_time=10s + repetitions=5 + aggregates
BENCHMARK(benchmark_command_roundtrip)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->Repetitions(5)
    ->ComputeStatistics("min", benchmark::StatsFunction::kMin)
    ->ComputeStatistics("max", benchmark::StatsFunction::kMax)
    ->ComputeStatistics("median", benchmark::StatsFunction::kMedian)
    ->ComputeStatistics("p95", [](const std::vector<double>& v) {
        return v[static_cast<size_t>(v.size() * 0.95)];
    });

// 示例：内存占用基准
static void benchmark_device_memory_idle(benchmark::State& state) {
    // 启动空闲设备端
    auto device = start_device_idle();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});  // 让 allocator 归还
    for (auto _ : state) {
        auto rss = get_rss_from_proc_status();  // /proc/self/status:VmRSS
        benchmark::DoNotOptimize(rss);
    }
    stop_device(device);
}
BENCHMARK(benchmark_device_memory_idle)->Unit(benchmark::kKilobyte);
```

#### 5.5.3 性能方法论（Round 4 新增）

为确保 33 项基准结果可复现、可比较、可检测回归，必须统一定义测量方法。

##### 5.5.3.1 硬件基线

| 项目 | 最低配置 | 推荐配置 |
|------|---------|---------|
| CPU | Intel Xeon Gold 6248 @ 2.5GHz × 8 核 | × 16 核 |
| 内存 | 16GB DDR4 | 64GB DDR4 |
| NIC | 1Gbps | 10Gbps |
| 磁盘 | SSD（NVMe） | NVMe |
| governor | performance | performance |
| 超线程 | 关闭 | 关闭 |

##### 5.5.3.2 构建配置

```cmake
# Release + 优化 + LTO + NDEBUG（性能基准专用）
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fno-omit-frame-pointer")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_FLAGS "-march=x86-64-v2")
```

CI 中 `benchmark` job 显式断言编译类型：
```bash
grep "CMAKE_BUILD_TYPE = Release" build-release/CMakeCache.txt
```

##### 5.5.3.3 测量前置条件

- **CPU 隔离**：`taskset -c 0-7` 绑核 + `SCHED_FIFO` 优先级 80
- **禁用 IRQ 迁移**：`echo 1 > /proc/irq/X/smp_affinity`
- **预热**：前 100 次迭代丢弃
- **样本量**：延迟基准 ≥ 10000 次、吞吐基准 ≥ 30s 时长
- **百分位计算**：HDR Histogram + 全程记录

##### 5.5.3.4 回归容差带

| 类型 | 软阈值（警告） | 硬阈值（阻断） |
|------|--------------|--------------|
| 延迟 | 目标 × 1.2 | 目标 × 1.5 |
| 吞吐 | 目标 × 0.8 | 目标 × 0.5 |
| 内存 | 目标 × 1.1 | 目标 × 1.3 |

##### 5.5.3.5 内存测量规范

```bash
# 使用 /proc/self/status:VmRSS（统一方法）
get_rss_kb() {
    awk '/VmRSS/ {print $2}' /proc/self/status
}

# 测量前 sleep 100ms 让 allocator 归还
sleep 0.1 && get_rss_kb
```

##### 5.5.3.6 性能基线存储

```bash
# 每夜 benchmark 结果存入 benchmarks_history.json
ctest --test-dir build-release -R "benchmark" \
    --benchmark_format=console \
    --benchmark_out=benchmark_result.json \
    --benchmark_out_format=json
# 上传到 artifacts 与历史对比
```

##### 5.5.3.7 硬件不可复现缓解

GitHub Actions `ubuntu-latest` 跨周硬件不一致，声明：
- 基准结果**仅作为回归检测参考**，不作为绝对性能契约
- 关键性能契约在 self-hosted runner 上验证（项目自有硬件）

##### 5.5.3.8 特定基准约束

- `#19 fork_exec_latency ≤ 80ms`：规定"预热 100 次后取 P95"、"SCHED_BATCH 优先级"、"关闭 ASLR"
- `#21 audit_write_throughput ≥ 10000 条/秒`：同步 fsync 模式（Round 4 提升）
- `#22 incremental_build < 30s`：规定"修改单一 .cpp 文件触发"，代码库 LOC ≤ 10 万行
- `#25 crash_recovery ≤ 5s`：WAL 状态"1000 条记录 / 10MB"，区分冷启动与热重启
- `#27 pki_handshake P95 < 50ms`：拆分为"PSK 复用"与"完整 PKI"两个基准
- `#29 soak 30 天`：改为"加速 24h 模拟 + 100% 流量"，避免 CI 阻塞

#### 5.5.4 性能契约外部依赖清单

| 基准 | 外部依赖 | 测试条件 |
|------|---------|---------|
| `#3 remote_command_roundtrip` | 同机房 LAN 1Gbps | RTT < 1ms |
| `#5 file_transfer_throughput` | 1MB chunk, payload=100MB | memfd（关闭磁盘 I/O） |
| `#9/#10 node_startup` | 仅初始化（不含 fork+exec） | nproc ≥ 8 |
| `#15 max_concurrent_nodes` | ulimit -n ≥ 4096 | vm.overcommit_memory=1 |
| `#11 inproc_latency` | 裸 IPC 延迟（不含序列化） | payload 64B |
| `#12 crypto_overhead` | AES-256-GCM + ChaCha20-Poly1305 | 明文 1KB + 1MB |

### 5.6 模糊测试策略

#### 5.6.1 模糊测试目标

| 目标 | 测试文件 | 输入格式 | 预期 |
|------|---------|---------|------|
| 协议编解码 | `fuzz_protocol_decode` | 随机字节 | 无 crash、无 ASAN 报告 |
| 加密握手 | `fuzz_crypto_handshake` | 随机字节 | 无 crash、优雅拒绝 |
| 配置解析 | `fuzz_config_parse` | 随机 YAML | 无 crash、优雅拒绝 |
| 消息构建 | `fuzz_message_build` | 随机字段值 | 无 crash、校验失败 |

#### 5.6.2 模糊测试实现

```cpp
// 示例：协议解码模糊测试（对齐03 §3.3.7 SerializerBase + Serializer<T>）
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    udaf::ability_b::serialization::SerializerBase base;
    // 不检查返回值，仅验证无 crash
    base.decode_raw(data, size);
    return 0;
}
```

### 5.7 压力测试策略

#### 5.7.1 压力测试场景

| # | 场景 | 测试文件 | 验证内容 | 出处 |
|---|------|---------|---------|------|
| 1 | 注册表满载 | `stress_registry_10000` | 10000 节点注册 + TTL 过期 + 批量清理 | 03 §13.1 |
| 2 | 滑动窗口 | `stress_sequence_64` | 64 sequence 滑动窗口 + 乱序 + 重传 | 03 §13.1 |
| 3 | 并发 Channel | `stress_concurrent_channels` | 1000 并发 Channel + 无死锁 | 03 §13.1 |
| 4 | 长时间运行 | `stress_long_running` | 30 天模拟（加速） + 无内存泄漏 | 03 §13.1 |
| 5 | **discovery 并发** | `stress_discovery_concurrent` | 100 个并发发现请求 + 无丢包 | Round 3 评审新增 |
| 6 | **discovery 稳定性** | `stress_discovery_stability` | 10000 次连续发现 + 注册表状态一致 | Round 3 评审新增 |
| 7 | **discovery 错误恢复** | `stress_discovery_error_recovery` | 注入随机网络错误 + 恢复机制验证 | Round 3 评审新增 |
| 8 | **dataflow 并发消息** | `stress_dataflow_concurrent_message` | 1000 条并发消息 + 顺序/完整性 | Round 3 评审新增 |
| 9 | **dataflow 稳定性** | `stress_dataflow_stability` | 3600 秒持续数据流 + 无内存泄漏 | Round 3 评审新增 |
| 10 | **dataflow 节点故障** | `stress_dataflow_node_failure` | 注入节点故障 + 重连/降级验证 | Round 3 评审新增 |

#### 5.7.2 压力测试实现

```cpp
// 示例：注册表满载压力测试
TEST(StressTest, Registry10000) {
    udaf::ability_a::registry::ServiceRegistry registry;
    const int kNodeCount = 10000;

    // 批量注册
    for (int i = 0; i < kNodeCount; ++i) {
        udaf::ability_a::registry::RegistryEntry entry;
        entry.node_id_ = fmt::format("node_{}", i);
        entry.bind_address_ = fmt::format("192.168.1.{}", i % 256);
        entry.bind_port_ = 5000;
        registry.upsert(entry);
    }

    // 验证容量
    EXPECT_EQ(registry.size(), kNodeCount);

    // 批量清理过期
    registry.cleanup_expired(std::chrono::seconds{0});
    EXPECT_EQ(registry.size(), 0);
}
```

### 5.8 Mock 策略

#### 5.8.1 核心 Mock 类

| Mock 类 | 被 Mock 接口 | 用途 |
|---------|------------|------|
| MockChannelBase | ChannelBase | 模拟通道行为 |
| TopologyUpdateCallbacks | TopologyUpdateCallbacks（std::function） | 模拟 A→B 回调 |
| MockWhitelistCheck | WhitelistCheck | 模拟白名单检查 |
| MockZmqSocket | zmq::socket_t | 模拟 ZMQ socket（03 §12.6） |
| MockAdvertiser | Advertiser | 集成测试：模拟 A 侧广播 |
| MockScanner | Scanner | 集成测试：模拟 A 侧扫描 |
| MockForkThread | ForkThread | 集成测试：模拟进程创建 |
| MockKeystore | Keystore | 集成测试：模拟密钥存储 |
| MockProcessExecutor | ProcessExecutor | 集成测试：模拟子进程执行 |

#### 5.8.2 Mock 实现

```cpp
// 示例：MockChannelBase（对齐 03 §3.3.5 ChannelBase 接口）
class MockChannelBase : public udaf::ability_b::transport::ChannelBase {
public:
    MOCK_METHOD(udaf::ability_b::transport::TransportType, type,
                (), (const, noexcept, override));
    MOCK_METHOD(udaf::ability_b::transport::RecvStatus, recv_bytes,
                (std::vector<std::byte>&, std::optional<std::chrono::milliseconds>),
                (override));
    MOCK_METHOD(void, send_bytes,
                (std::span<const std::byte>, udaf::ability_b::transport::MessagePriority),
                (override));
    MOCK_METHOD(udaf::ability_b::transport::SendResult, try_send_bytes,
                (std::span<const std::byte>, udaf::ability_b::transport::MessagePriority),
                (override));
};

// 示例：TopologyUpdateCallbacks（对齐 03 §2.3.2，是 struct 不是虚函数类）
udaf::bridge::TopologyUpdateCallbacks callbacks;
callbacks.on_node_added = [](const udaf::ability_a::registry::RegistryEntry& entry) -> Result<void> {
    // 验证节点已添加
    return Result<void>{};
};
callbacks.on_node_removed = [](const std::string& node_id) -> Result<void> {
    return Result<void>{};
};
```

### 5.9 测试数据管理

#### 5.9.1 测试数据分类

| 类型 | 存储位置 | 管理方式 |
|------|---------|---------|
| 单元测试数据 | 临时目录 | SetUp 创建，TearDown 清理 |
| 集成测试数据 | 临时目录 | 完整生命周期管理 |
| 性能基准数据 | 临时目录 | 测试后清理 |
| 模糊测试语料 | `tests/fuzz/corpus/` | Git 管理，CI 累积 |

#### 5.9.2 测试配置文件

```yaml
# tests/config/test_config.yaml
discovery:
  broadcast_interval_ms: 100  # 测试用短间隔
  scan_timeout_ms: 500
  registry_ttl_s: 5

topology:
  max_nodes: 1000
  wal_path: "/tmp/udaf_test/wal"

scheduler:
  max_restart_attempts: 3
  restart_backoff_ms: 100

encryption:
  psk: "test-psk-key-for-unit-tests"
```

### 5.10 CI/CD 测试流水线

#### 5.10.1 流水线阶段

```mermaid
flowchart LR
    A[代码提交] --> B[编译]
    B --> C[静态分析]
    C --> D[单元测试]
    D --> E[集成测试]
    E --> F[性能基准]
    F --> G[覆盖率报告]
    G --> H[合并/发版]
```

#### 5.10.2 阶段详情

| 阶段 | 工具 | 超时 | 阻断级别 |
|------|------|------|---------|
| 编译 | CMake + GCC/Clang | 5min | 阻断 |
| 静态分析 | clang-tidy + cppcheck | 10min | 阻断（0 error） |
| 单元测试 | GoogleTest + CTest | 10min | 阻断 |
| 集成测试 | GoogleTest + subprocess | 15min | 阻断 |
| 性能基准 | Google Benchmark | 20min | 阻断发版 |
| 模糊测试 | libFuzzer | 30min | 阻断合并（每周） |
| 压力测试 | 自定义 | 60min | 阻断发版（发版前） |
| 覆盖率 | gcov + lcov | 10min | 报告 |

#### 5.10.3 覆盖率报告

```bash
# 生成覆盖率报告
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build --output-on-failure
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_report
```

### 5.12 测试辅助库（Round 4 新增）

集中测试构造逻辑，避免重复。

| 辅助函数 | 位置 | 用途 |
|---------|------|------|
| `make_sample_service_entry(id, addr)` | `tests/include/udaf_test_helpers/service.h` | 构造 ServiceEntry 测试数据 |
| `make_random_payload(size)` | `tests/include/udaf_test_helpers/payload.h` | 生成随机字节 payload |
| `make_wal_with_n_entries(n)` | `tests/include/udaf_test_helpers/wal.h` | 构造含 N 条记录的 WAL 文件 |
| `make_self_signed_cert(common_name)` | `tests/include/udaf_test_helpers/cert.h` | 生成自签名测试证书 |
| `make_out_of_order_sequence_array(n)` | `tests/include/udaf_test_helpers/sequence.h` | 生成乱序 sequence 数组 |
| `make_sample_audit_log_entry(action)` | `tests/include/udaf_test_helpers/audit.h` | 构造 AuditLogEntry |
| `make_sample_yaml_config(profile)` | `tests/include/udaf_test_helpers/config.h` | 生成测试 YAML 配置（unit/integration/stress/bench） |

**目录结构**：

```
tests/
├── include/udaf_test_helpers/
│   ├── service.h
│   ├── payload.h
│   ├── wal.h
│   ├── cert.h
│   ├── sequence.h
│   ├── audit.h
│   └── config.h
├── unit/
├── integration/
├── stress/
├── benchmark/
├── fuzz/
└── config/
    ├── unit/
    ├── integration/
    ├── stress/
    └── bench/
```

### 5.13 ABI 兼容性测试（Round 4 新增）

> 对齐 §6.1 验收要求："ABI 兼容性检查策略明确（semver + ABI diff 工具）"。

| 工具 | 版本 | 用途 |
|------|------|------|
| abigail-tools | >= 2.0 | ABI 差异分析 |
| abi-compliance-checker | >= 2.0 | ABI 兼容性验证 |
| abidiff | >= 2.0 | 增量 ABI diff |

**测试策略**：
- 每个 release tag 与前一个 tag 做 ABI 对比
- 0 警告通过（任何符号变化、类型变化、虚函数表变化必须显式标注）
- 头文件 ABI 稳定性：导出符号、大小、布局、对齐

```bash
# CI 中执行
abidiff previous.so current.so --no-show-locs
# 0 警告通过（破坏性变更必须升 major + CHANGELOG）
```

### 5.14 风险与缓解（Round 4 新增）

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| CI 容器资源共享导致性能基准波动 | 性能结果不可信 | 取 P50 而非 P95；对比相对基线 |
| 8 线程并发测试在 2-vCPU runner 上脆弱 | CI 偶发失败 | 标记 `DISABLED_` 并提供手动触发 |
| GCC 14 gcov 格式与 lcov 2.0 不兼容 | 覆盖率数据丢失 | 固定 GCC 13 / lcov 2.2 |
| 集成测试端口冲突 | 偶发端口占用失败 | 分配 19000-19999 端口池 |
| 模糊测试发现 crash 但 CI 默认不阻塞 | 安全问题遗漏 | `if: always()` 上传 artifact + 强制修复 |
| OpenSSL 3.0 与部分 aarch64 工具链不兼容 | 设备端构建失败 | 明确支持的工具链版本清单 |
| 性能基准回归容差缺失 | 偶发抖动阻塞 CI | 软阈值 ×1.2 + 硬阈值 ×1.5 |
| 编译产物未传递导致下游 job 失败 | CI 流水线中断 | Round 4 修复：upload-artifact + download-artifact |
| 文档版本号引用不一致 | 评审/实现混乱 | 统一 02 v2.10 / 03 v2.3 / 04 v0.9 / 05 v0.9 |
| 03/04 模块设计尚未完全对齐 05 测试场景 | 测试代码无法落地 | v0.6 已部分同步，剩余待实现阶段 |
| 性能基准数量 02/03（33 项）vs 05（33 项）已对齐 | 跨文档引用断裂 | ✅ v0.8 已修复：02 §3.4 + 03 §11.2 补齐 #30~#33（aead_throughput_1mb / audit_verify_chain / wal_replay_full / topology_commit_50），三文档全部 33 项 |

### 5.11 测试环境要求

#### 5.11.1 开发环境

| 依赖 | 版本 | 用途 |
|------|------|------|
| GCC/Clang | >= 12（GCC）、>= 15（Clang） | 编译（C++20） |
| CMake | >= 3.20 | 构建 |
| GoogleTest | >= 1.14 | 单元测试 |
| Google Benchmark | >= 1.8 | 性能基准 |
| libFuzzer | LLVM 内置（Clang >= 15） | 模糊测试 |
| lcov | >= 1.16 | 覆盖率（需与 GCC gcov 版本匹配） |
| fmt | >= 9.0 | 格式化输出 |
| yaml-cpp | >= 0.7 | 配置解析 |
| libzmq | >= 4.3 | 消息中间件 |
| **OpenSSL** | **>= 3.0** | **TLS/加密（HMAC/TLS/PSK/PKI）** |
| AddressSanitizer | Clang 内置 | 内存错误检测 |
| ThreadSanitizer | Clang 内置 | 数据竞争检测 |
| UBSanitizer | Clang 内置 | 未定义行为检测 |

#### 5.11.2 支持平台

| 平台 | 架构 | 状态 | 说明 |
|------|------|------|------|
| Linux（主机） | x86_64 | ✅ 主要 | CI 与开发环境 |
| Linux（主机） | aarch64 | ✅ 支持 | ARM 服务器 |
| 嵌入式 Linux（设备） | aarch64 | ✅ 支持 | 交叉编译（需提供工具链） |
| 嵌入式 Linux（设备） | armv7 | ✅ 支持 | 交叉编译 |
| 嵌入式 Linux（设备） | riscv64 | 🔵 实验 | 交叉编译 |
| Windows | x86_64 | ❌ 不支持 | 仅 Linux |
| macOS | x86_64/aarch64 | ❌ 不支持 | 仅 Linux |

**交叉编译要求**：
- 提供 `scripts/setup-cross-compile.sh` 脚本（含工具链检查、sysroot 配置）
- 提供 `build/device-aarch64-toolchain.cmake` 工具链文件
- 设备端测试通过 QEMU 用户态模拟运行（`qemu-aarch64-static`）

#### 5.11.3 CI 环境

```yaml
# .github/workflows/test.yml
name: Tests
on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]
  schedule:
    - cron: '0 2 * * 0'  # 每周日凌晨 2 点触发模糊测试

jobs:
  # 阶段 1：并行编译（Debug + Release），上传到 artifact
  build-debug:
    runs-on: ubuntu-latest
    timeout-minutes: 15
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ libzmq3-dev libyaml-cpp-dev libfmt-dev libssl-dev clang-tidy cppcheck lcov ccache
          echo "CCACHE_DIR=$HOME/.ccache" >> $GITHUB_ENV
      - name: Configure (Debug)
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      - name: Build (Debug)
        run: cmake --build build -j$(nproc)
      - name: Upload Debug artifact
        uses: actions/upload-artifact@v4
        with:
          name: build-debug-${{ github.sha }}
          path: build/
          retention-days: 1

  build-release:
    runs-on: ubuntu-latest
    timeout-minutes: 15
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ libzmq3-dev libyaml-cpp-dev libfmt-dev libssl-dev ccache
          echo "CCACHE_DIR=$HOME/.ccache" >> $GITHUB_ENV
      - name: Configure (Release)
        run: cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      - name: Build (Release)
        run: cmake --build build-release -j$(nproc)
      - name: Upload Release artifact
        uses: actions/upload-artifact@v4
        with:
          name: build-release-${{ github.sha }}
          path: build-release/
          retention-days: 1

  # 阶段 2：并行静态分析（不依赖编译产物）
  static-analysis:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get install -y clang-tidy cppcheck
      - name: clang-tidy
        run: |
          cmake -B build-tidy -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
          find udaf/src -name '*.hpp' -o -name '*.cpp' | xargs clang-tidy -p build-tidy 2>&1 | tee tidy.log
          if grep -E "error:" tidy.log; then exit 1; fi
      - name: cppcheck
        run: cppcheck --enable=all --error-exitcode=1 udaf/src/

  # 阶段 3：单元/集成测试（下载 Debug artifact）
  test:
    needs: [build-debug]
    runs-on: ubuntu-latest
    timeout-minutes: 30
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@v4
      - name: Download Debug artifact
        uses: actions/download-artifact@v4
        with:
          name: build-debug-${{ github.sha }}
          path: build/
      - name: Install OpenSSL runtime
        run: sudo apt-get install -y libssl-dev
      - name: Unit tests
        run: ctest --test-dir build --output-on-failure -E "integration|stress|benchmark|fuzz"
      - name: Integration tests
        run: ctest --test-dir build --output-on-failure -R "integration"

  # 阶段 4：性能基准（下载 Release artifact）
  benchmark:
    needs: [build-release]
    runs-on: ubuntu-latest
    timeout-minutes: 30
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@v4
      - name: Download Release artifact
        uses: actions/download-artifact@v4
        with:
          name: build-release-${{ github.sha }}
          path: build-release/
      - name: Install OpenSSL runtime
        run: sudo apt-get install -y libssl-dev
      - name: Performance benchmarks
        run: ctest --test-dir build-release --output-on-failure -R "benchmark" --timeout 1200

  # 阶段 5：覆盖率报告
  coverage:
    needs: [test]
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download Debug artifact
        uses: actions/download-artifact@v4
        with:
          name: build-debug-${{ github.sha }}
          path: build/
      - name: Install lcov
        run: sudo apt-get install -y lcov
      - name: Coverage report
        run: |
          lcov --capture --directory build --output-file coverage.info --rc lcov_branch_coverage=1
          lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info --rc lcov_branch_coverage=1
          genhtml coverage_filtered.info --output-directory coverage_report --branch-coverage
          lcov --list coverage_filtered.info --rc lcov_branch_coverage=1

  # 阶段 6：模糊测试（仅 schedule 触发）
  fuzz:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    steps:
      - uses: actions/checkout@v4
      - name: Install Clang
        run: sudo apt-get install -y clang
      - name: Configure (Fuzz)
        env:
          CC: clang
          CXX: clang++
        run: cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug -DENABLE_FUZZ=ON
      - name: Build (Fuzz)
        run: cmake --build build-fuzz -j$(nproc)
      - name: Run fuzz tests
        run: ctest --test-dir build-fuzz -R "fuzz" --timeout 3600

  # 阶段 7：压力测试（仅发版 tag 触发）
  stress:
    runs-on: ubuntu-latest
    if: startsWith(github.ref, 'refs/tags/v')
    steps:
      - uses: actions/checkout@v4
      - name: Configure (Stress)
        run: cmake -B build-stress -DCMAKE_BUILD_TYPE=Release
      - name: Build (Stress)
        run: cmake --build build-stress -j$(nproc)
      - name: Run stress tests
        run: ctest --test-dir build-stress --output-on-failure -R "stress" --timeout 3600
```

---

## 6. 验收标准

### 6.1 阶段 5（测试方案）验收

- [ ] 测试分类完整（单元/集成/性能/模糊/压力）
- [ ] 测试命名规范与03 §13.2 一致
- [ ] 每个模块有对应的测试文件清单
- [ ] 性能基准与02 §3.4 一致（33 项）
- [ ] 集成测试链路与03 §10 一致（5 条）
- [ ] Mock 策略完整
- [ ] CI/CD 流水线设计完整
- [ ] 覆盖率目标明确
- [ ] 测试环境要求明确
- [ ] 公共 API 有 Doxygen 使用说明文档
- [ ] ABI 兼容性检查策略明确（semver + ABI diff 工具）
- [ ] **不包含实现代码**（仅测试方案）

### 6.2 实现阶段输入

进入实现阶段前需明确：

1. **测试文件实际创建**：按 §5.3.1 清单创建测试文件
2. **测试数据准备**：准备性能基准测试数据
3. **CI 环境搭建**：配置 GitHub Actions / GitLab CI
4. **覆盖率基线**：建立初始覆盖率基线

---

## 7. 附录

### 7.1 测试用例统计

| 模块 | 测试文件数 | 测试用例数 | 目标覆盖率 |
|------|-----------|-----------|-----------|
| udaf::core | 8 | ~40 | > 90% |
| udaf::ability_a::discovery | 3 | ~16 | > 90% |
| udaf::ability_a::network | 2 | ~8 | > 90% |
| udaf::ability_a::parameters | 2 | ~7 | > 90% |
| udaf::ability_a::encryption | 1 | ~6 | > 90% |
| udaf::crypto | 5 | ~18 | > 90% |
| udaf::platform | 3 | ~10 | > 90% |
| udaf::ability_b::topology | 1 | ~6 | > 90% |
| udaf::ability_b::scheduler | 1 | ~6 | > 90% |
| udaf::ability_b::channel | 2 | ~6 | > 90% |
| udaf::ability_c | 4 | ~19 | > 85% |
| udaf::bridge | 2 | ~6 | > 90% |
| udaf::cli | 2 | ~6 | > 80% |
| udaf::audit | 2 | ~7 | > 90% |
| udaf::sdk | 1 | ~3 | > 85% |
| udaf::observability | 1 | ~3 | > 80% |
| **总计** | **~38** | **~150+** | — |

### 7.2 测试代码与实现代码映射

| 实现文件 | 测试文件 |
|---------|---------|
| `core/result.hpp` | `tests/core/test_result.cc` |
| `core/unique_fd.hpp` | `tests/core/test_unique_fd.cc` |
| `core/error_code.hpp` | `tests/core/test_error_code.cc` |
| `core/config.cc` | `tests/core/test_config.cc` |
| `ability_b/serialization/serializer.cpp` | `tests/core/test_serializer.cc` |
| `core/wal.cc` | `tests/core/test_wal.cc` |
| `ability_a/discovery/advertiser.cc` | `tests/ability_a/test_advertiser.cc` |
| `ability_a/discovery/scanner.cc` | `tests/ability_a/test_scanner.cc` |
| `ability_a/registry/service_registry.cc` | `tests/ability_a/test_service_registry.cc` |
| `ability_a/network/message_handler.cc` | `tests/ability_a/test_message_handler.cc` |
| `ability_a/parameters/parameter_handler.cc` | `tests/ability_a/test_parameter_handler.cc` |
| `ability_a/encryption/crypto.cc` | `tests/ability_a/test_crypto.cc` |
| `ability_b/topology/topology.cc` | `tests/ability_b/test_topology.cc` |
| `ability_b/scheduler/scheduler.cc` | `tests/ability_b/test_scheduler.cc` |
| `ability_c/cmd_exec_node.cc` | `tests/ability_c/test_cmd_exec_node.cc` |
| `ability_c/file_xfer_node.cc` | `tests/ability_c/test_file_xfer_node.cc` |
| `ability_c/heartbeat_node.cc` | `tests/ability_c/test_heartbeat_node.cc` |
| `ability_c/net_info_node.cc` | `tests/ability_c/test_net_info_node.cc` |
| `bridge/discovery_bridge.cc` | `tests/bridge/test_discovery_bridge.cc` |
| `audit/audit_logger.cc` | `tests/audit/test_audit_logger.cc` |
| `sdk/udaf_c_api.cc` | `tests/sdk/test_udaf_c_api.cc` |
| `observability/metrics_collector.cc` | `tests/observability/test_metrics_collector.cc` |

### 7.3 缩略语

| 缩略语 | 全称 |
|--------|------|
| ASAN | AddressSanitizer |
| CI | Continuous Integration |
| CD | Continuous Delivery |
| COW | Copy-On-Write |
| CTest | CMake Test |
| DFRM | Device Framework（ref/ 项目） |
| GTest | GoogleTest |
| MVP | Minimum Viable Product |
| PSK | Pre-Shared Key |
| RSS | Resident Set Size |
| WAL | Write-Ahead Log |
| ZMQ | ZeroMQ（消息中间件库） |

### 7.4 变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v0.1 | 2026-08-27 | 初稿 |
| v0.2 | 2026-08-27 | **Round 1 评审修复**：§1 拆分为独立"背景"和"目标"章节；新增 §4 通过标准；补充 3 项缺失性能基准（crash_recovery/tcp_latency_p99/pki_handshake）；fork+exec 目标值统一为 ≤80ms；新增 udaf::crypto（5 文件）和 udaf::platform（3 文件）测试覆盖；增强集成测试链路描述（debounce/白名单/RAII）；补充 5 个集成测试 Mock 类；CI 流水线补充静态分析/模糊测试/性能基准步骤；统一 GoogleTest 命名；补充 ZMQ 缩略语 |
| v0.3 | 2026-08-27 | **Round 2 评审修复**：修复章节编号错误（§4/§6 子节编号）；删除不存在的测试文件（NodeFactory/ConfigParser/Coordinator/ChannelManager）；MockChannel 改为继承 ChannelBase；MockTopologyUpdateCallbacks 改为 std::function struct；验收标准数量统一为 29 项；更新编译器版本（GCC>=12/Clang>=15）；补充缺失依赖（fmt/yaml-cpp/libzmq）；补充验收标准（API 文档/ABI 稳定性） |
| v0.4 | 2026-08-27 | **Round 3 评审修复**：修复 MockChannelBase 返回类型错误（send_bytes→void/try_send_bytes→SendResult/type→TransportType）；统一性能基准数量为 29 项（§6.1）；基准测试函数命名改为 snake_case（benchmark_command_roundtrip 等）；新增 §5.3.0 错误码测试矩阵（16 个核心错误码）；补充压力测试场景（discovery/dataflow 共 6 项）；补充边界/并发/错误恢复测试用例（14 项）；补充重试/降级/故障转移测试（5 项）；新增 OpenSSL >= 3.0 依赖与嵌入式 Linux 平台说明；CI YAML 重构为并行流水线（Debug+Release 并行编译、静态分析独立、schedule 触发 fuzz、tag 触发 stress）；新增 §4.2.2 指标级覆盖率与 §4.2.3 模糊测试专属指标；修复 §5.11.2/§5.11.3 章节编号重复 |
| v0.5 | 2026-08-27 | **Round 4 评审修复 + 设计文档对齐**：①设计文档调整：02 升 v2.7、03 引用 02 v2.7、04 补版本号 v0.7。②Critical 修复：CI YAML 编译产物传递（upload-artifact + download-artifact + permissions + timeout-minutes + ccache + job 矩阵）。③安全测试 30 项缺口补充（加密7 + 认证5 + 输入验证6 + 权限7 + 审计5，对齐 CLAUDE.md §3 关键约束#1/#7）。④性能方法论（§5.5.3）：硬件基线 + 构建配置 + 测量前置条件 + 回归容差带 + 内存测量规范 + 基线存储。⑤集成测试真实性（§5.4.2-5.4.6）：真实网络资源分配 + 显式断言 + 失败注入矩阵 + 数据一致性验证 + 能力 C 端到端 4 条链路。⑥新增 §5.12 测试辅助库、§5.13 ABI 兼容性测试、§5.14 风险与缓解。⑦负面断言统一 `test_<class>_<method>_neg_<scenario>` 前缀。⑧udaf::core 测试文件 10→8 修正。⑨测试用例总数更新至 ~175+。 |
| v0.6 | 2026-08-28 | **Round 5 评审修复 + 类型/命名空间对齐 03/04**：①Critical 修复：CI YAML `coverage` job 补充 `download-artifact`（v0.5 修复后回归遗漏）。②命名空间修正：`udaf::Client` → `udaf::sdk::Client`（§5.4.2，对齐03 §1.3/§3.5.1）；`udaf::Client` 成员声明同步。③类型修正：`ServiceEntry` → `RegistryEntry`（§5.4.5/§5.7.2，对齐03 §2.3.2），字段名补尾下划线（`.node_id_`/`.bind_address_`/`.bind_port_`）。④序列化类型修正：`ProtocolSerializer` + `MessageEnvelope` 不存在03（03 §3.3.7 用 `SerializerBase` + `Serializer<T>`），§5.3.1 测试文件 `test_protocol_serializer.cc` → `test_serializer.cc`、§5.3.0/§5.3.1.1 引用同步、§5.6.2 模糊测试改用 `SerializerBase::decode_raw`。⑤§5.3.1 udaf::core 头部数量同步（10 文件/~50 用例 → 8 文件/~38 用例，对齐实际表格）。⑥§5.5.3.8 audit_write_throughput 值回退到 ≥ 1000 条/秒（v0.5 误改 10000 未核对02 原文）。⑦已知问题登记：性能基准数量 02/03（24 项）vs 05（29 项）仍不一致，待 v0.7 统一。 |
| v0.7 | 2026-08-28 | **Round 6 性能契约对齐 02/03**：①02 §3.4 性能契约表 24→29 项（新增 #25 命令往返延迟 P99 < 15ms / #26 加密吞吐损失 < 20% / #27 审计写入吞吐 ≥ 1000 条/秒 / #28 设备端峰值内存 < 16MB / #29 主机端峰值内存 < 128MB），版本升 v2.7 → v2.8。②03 §11 性能契约 24→29 项，版本升 v2.0 → v2.1。③04 前置引用同步（02 v2.8 + 03 v2.1）。④05 头部前置引用同步（02 v2.8 + 03 v2.1）。⑤§5.14 风险表已知问题关闭：性能基准数量 02/03/05 三方全部 29 项，已对齐。⑥§5.14 风险表版本号引用更新：02 v2.8 / 03 v2.1 / 04 v0.7 / 05 v0.7。 |
| v0.8 | 2026-09-01 | **Round 7 性能契约对齐 v0.3.13 实现新增 4 项**：①02 §3.4 性能契约表 29→33 项（新增 #30 AEAD 大块吞吐 / #31 审计链校验 / #32 WAL 完整 replay / #33 拓扑事务批量 commit），版本升 v2.8 → v2.9。②03 §11 性能契约 29→33 项，版本升 v2.1 → v2.2。③04 前置引用同步（02 v2.9 + 03 v2.2），版本升 v0.7 → v0.8。④05 头部前置引用同步（02 v2.9 + 03 v2.2 + 04 v0.8），§5.5.1 表追加 #30~#33。⑤§5.14 风险表已知问题关闭：性能基准数量 02/03/04/05 四方全部 33 项，已对齐。⑥§5.14 风险表版本号引用更新：02 v2.9 / 03 v2.2 / 04 v0.8 / 05 v0.8。 |
| v0.9 | 2026-09-01 | **ADR 索引同步**：架构文档附录 B 升级（新增"状态"+"关键决策"列），ADR-001~010 状态批量更新为"已批准"；架构版本升 v2.9 → v2.10；头部前置引用同步；§5.14 风险表版本号引用更新（02 v2.10） |
| v0.3.16 | 2026-09-03 | **F22~F25 实现收尾**：libudaf_fi.c 警告清零 + C/CXX 编译选项分流 + pki.cpp 0%→92.3% 覆盖 + CHANGELOG/版本表同步；618 测试全绿；33 项性能契约 100% PASS |
