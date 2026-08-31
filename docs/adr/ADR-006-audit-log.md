# ADR-006: 审计日志模块

> **状态**：已批准
> **日期**：2026-08-26（提议）
> **批准日期**：2026-09-01（实现阶段完成，446/446 测试通过，0 编译警告）
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §6.2 / §10.1
> **响应需求**：CLAUDE.md 关键约束"所有命令执行、文件传输、配置变更必须记录" + 需求 §6.2 + §10.1 MVP"审计日志最小能力"

---

## 1. 背景

需求 §6.2 第 5 项硬约束："审计日志完整可追溯——所有命令执行、文件传输、配置变更必须记录"。§10.1 MVP 要求"审计日志最小能力（记录操作员/时间戳/设备 ID/动作/参数，本地保存 7 天）"。架构 v2.0 全文无审计模块。

## 2. 决策

**新增 `audit::Logger` 模块**，独立于 WAL（崩溃恢复），职责为结构化审计事件记录。

### 2.1 审计事件结构

```cpp
namespace udaf::audit {

struct AuditEvent {
    uint64_t    timestamp_ns;       // 单调时钟
    char        event_id[16];       // UUID（事件唯一标识）
    char        actor_id[64];       // 操作员标识（设备/主机 NodeId + 业务用户名）
    char        actor_ip[46];       // 源 IP（支持 IPv6）
    NodeId      target_device;      // 目标设备
    ActionType  action;             // 动作类型
    char        params_hash[64];    // 参数摘要（SHA-256，不记录明文密码）
    uint32_t    result_code;        // UDAF 错误码
    char        prev_hash[64];      // 前一条事件的 SHA-256（hash chain 防篡改）
};

enum class ActionType : uint8_t {
    CMD_EXEC          = 0x01,     // 命令执行
    FILE_PUSH         = 0x02,     // 文件上传
    FILE_PULL         = 0x03,     // 文件下载
    CONFIG_CHANGE     = 0x04,     // 配置变更
    NODE_START        = 0x05,     // 节点启动
    NODE_STOP         = 0x06,     // 节点停止
    AUTH_EVENT        = 0x07,     // 认证事件（成功/失败）
    WHITELIST_CHANGE  = 0x08,     // 白名单变更
    DEVICE_INFO_CHANGE= 0x09,     // 设备信息变更（model/serial/fw_version）
    NETWORK_CHANGE    = 0x0A,     // 网络配置变更（ip/gateway/dns）
    DEVICE_ONLINE     = 0x10,     // 设备上线
    DEVICE_OFFLINE    = 0x11,     // 设备离线
    SCHEDULE_REQUEST  = 0x12,     // 调度请求（成功/拒绝）
};

}  // namespace
```

### 2.2 hash chain 机制

每条事件包含 `prev_hash`（前一条事件的 SHA-256），形成链式结构：

```
event_1.prev_hash = genesis_hash
event_2.prev_hash = SHA-256(event_1)
event_3.prev_hash = SHA-256(event_2)
```

**创世块（genesis）安全性**：

```cpp
// 创世 hash 计算：避免固定常量导致攻击者可预设伪造链
Hash genesis_hash() noexcept {
    // 三源混合：设备唯一标识（NodeId）+ OS 启动随机数 + 首次启动时间
    Hash h{};
    const auto node_id = platform::get_persistent_node_id();  // /etc/machine-id 或 TPM 派生
    const auto boot_random = platform::get_boot_random();    // 启动时从 /dev/urandom 读 32B
    const auto boot_time = platform::monotonic_ns();         // 首次启动时间
    h = SHA-256(node_id || boot_random || boot_time);
    return h;
}
```

**约束**：
- **禁止**使用全零常量或可预测字符串作为创世 hash
- `boot_random` 必须在每次 OS 启动后从 `/dev/urandom` 重读，不能持久化
- `genesis_hash` 在首次启动时一次性计算并缓存到只读文件 `/var/lib/udaf/audit_genesis.bin`（0600 权限）
- 文件被删后重启会引发"创世漂移"告警（v1.0 接受漂移，v1.x 强制要求外部锚定）

**优势**：任何事件的篡改会导致后续所有哈希断裂，便于事后审计完整性校验。攻击者无法通过预测 genesis 来预先生成伪造链。

### 2.3 存储与保留策略

| 维度 | 策略 |
|------|------|
| **本地文件** | `/var/log/udaf/audit/YYYY-MM-DD.log`（JSONL 格式） |
| **保留期** | 7 天（MVP），v1.x 可配置 1-90 天 |
| **磁盘占用估算** | 100 条/秒 × 1KB/条 × 86400s = ~8.6GB/天（需 rotate + 压缩） |
| **rotation** | 按天切割 + gzip 压缩（压缩后约 10%） |
| **远程聚合** | v1.x 提供 `audit::RemoteExporter` 接口，推送到中心日志系统 |

### 2.4 性能影响

| 指标 | 约束 |
|------|------|
| 写入延迟 | < 100μs（异步批量写入） |
| CPU 开销 | < 1%（SHA-256 增量计算） |
| 磁盘 I/O | 与 WAL 分离独立文件，避免互相影响 |

### 2.5 接入点

| 调用方 | 埋点位置 |
|--------|---------|
| `cmd_exec_node` | 命令执行前/后 |
| `file_xfer_node` | 文件传输完成时 |
| `Topology::load_from_yaml` | 配置加载/变更时 |
| `ServiceRegistry::upsert` | 设备上线/离线时 |
| `Authenticator::*` | 认证成功/失败时 |
| `PeerWhitelist::*` | 白名单变更时 |

## 3. 后果

- ✅ 满足需求 §6.2 审计日志硬约束
- ✅ hash chain 防篡改（v1.0 基础安全）
- ⚠️ 100 条/秒场景需高效 rotate + 压缩策略
- ⚠️ v1.0 不支持不可篡改存储（v1.x 评估 append-only 文件系统）

## 4. 引用

- [CLAUDE.md §3 关键约束](../../CLAUDE.md#3-不可违反的关键约束)
- [需求 §6.2 安全硬约束](../01-requirements.md#62-安全硬约束)
- [需求 §10.1 MVP 必含](../01-requirements.md#101-v10-mvp-必含)
