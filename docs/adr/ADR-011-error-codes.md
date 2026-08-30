# ADR-011: ErrorCode 统一定义

> **状态**：已批准
> **日期**：2026-08-27
> **前置**：[`docs/01-requirements.md`](../01-requirements.md) v1.0 §4.4 F-X-01 + [`docs/02-architecture.md`](../02-architecture.md) v2.4 §6.1

---

## 1. 背景

需求 §4.4 F-X-01 要求"统一的错误码体系，覆盖协议 / 网络 / 加密 / 业务四类"。ErrorCode 被以下文档引用：

| 文档 | 引用方式 |
|------|---------|
| 02 架构 §6 | 定义枚举 + C 聚合桶 |
| 03 概要设计 §8 | 重复定义枚举 + 错误字符串 + CLI 退出码 |
| 04 详细设计 §2 | 代码中使用（命名不一致） |
| 05 测试方案 | 测试用例引用错误码 |

多处定义导致命名不一致（`SCREAMING_SNAKE` vs `kPascalCase`）和值不同步。本 ADR 作为 **ErrorCode 单一权威源**，其他文档只引用，不重复定义。

---

## 2. 决策

### 2.1 命名约定

- **C++ 枚举值**：`SCREAMING_SNAKE_CASE`（如 `NET_TIMEOUT`）
- **命名空间**：`udaf::core`
- **基础类型**：`uint32_t`（便于 FFI）
- **文件后缀**：枚举定义放在各模块头文件中，本 ADR 只规定值与规则

### 2.2 分段规则

| 段 | 范围 | 用途 |
|----|------|------|
| 成功 | `0x0000` | `OK` |
| 协议 | `0x1000-0x1FFF` | 消息格式 / 版本 / 校验 |
| 网络 | `0x2000-0x2FFF` | 连接 / 传输 / 超时 |
| 加密 | `0x3000-0x3FFF` | TLS / PSK / 证书 |
| 业务 | `0x4000-0x4FFF` | 命令执行 / 文件 / 认证 |
| 资源 | `0x5000-0x5FFF` | 内存 / CPU / FD 耗尽 |
| 序列化 | `0x6000-0x6FFF` | 编解码 / 版本 / 类型 |
| 配置 | `0x7000-0x7FFF` | 解析 / 校验 |
| 拓扑/发现 | `0x8000-0x8FFF` | 图结构 / 对端发现 |
| 节点 | `0x9000-0x9FFF` | 生命周期 |
| 通用 | `0xF000-0xFFFF` | 兜底 / 参数 / 内部 |

新增错误码必须在对应段内分配下一个可用值，禁止跨段。

### 2.3 完整枚举

```cpp
namespace udaf::core {

enum class ErrorCode : uint32_t {
    OK                              = 0,

    // 协议错误 0x1000-0x1FFF
    PROTOCOL_INVALID_MAGIC          = 0x1001,
    PROTOCOL_VERSION_MISMATCH       = 0x1002,
    PROTOCOL_INVALID_MSG_TYPE       = 0x1003,
    PROTOCOL_PAYLOAD_TOO_LARGE      = 0x1004,
    PROTOCOL_CHECKSUM_MISMATCH      = 0x1005,
    PROTOCOL_PROTOCOL_REJECTED      = 0x1006,
    PROTOCOL_TRUNCATED_BUFFER       = 0x1007,

    // 网络错误 0x2000-0x2FFF
    NET_TIMEOUT                     = 0x2001,
    NET_HOST_UNREACHABLE            = 0x2002,
    NET_CONNECTION_REFUSED          = 0x2003,
    NET_SOCKET_CLOSED               = 0x2004,
    NET_BROADCAST_FAILED            = 0x2005,
    NET_PARTITION_DETECTED          = 0x2006,
    NET_RATE_LIMITED                = 0x2007,
    NET_SEND_FAILED                 = 0x2008,
    NET_NOT_CONNECTED               = 0x2009,

    // 加密错误 0x3000-0x3FFF
    CRYPTO_HMAC_MISMATCH            = 0x3001,
    CRYPTO_TLS_HANDSHAKE_FAILED     = 0x3002,
    CRYPTO_PSK_MISMATCH             = 0x3003,
    CRYPTO_CERT_EXPIRED             = 0x3004,
    CRYPTO_CERT_UNTRUSTED           = 0x3005,
    CRYPTO_CERT_CHAIN_INVALID       = 0x3006,
    CRYPTO_CA_UNREACHABLE           = 0x3007,
    CRYPTO_CRL_CHECK_FAILED         = 0x3008,
    CRYPTO_CERT_REVOKED             = 0x3009,
    CRYPTO_HANDSHAKE_FAILED         = 0x300A,
    CRYPTO_NONCE_REUSED             = 0x300B,
    CRYPTO_DOWNGRADE_REJECTED       = 0x300C,

    // 业务错误 0x4000-0x4FFF
    BIZ_CMD_EXEC_FAILED             = 0x4001,
    BIZ_FILE_NOT_FOUND              = 0x4002,
    BIZ_FILE_PERMISSION_DENIED      = 0x4003,
    BIZ_SHELL_METACHAR_REJECTED     = 0x4004,
    BIZ_SERVICE_NOT_FOUND           = 0x4005,
    BIZ_NODE_NOT_REGISTERED         = 0x4006,
    BIZ_PROTOCOL_VERSION_TOO_OLD    = 0x4007,
    BIZ_AUTH_UNTRUSTED              = 0x4008,

    // 资源耗尽 0x5000-0x5FFF
    RES_MEMORY_EXHAUSTED            = 0x5001,
    RES_CPU_OVERLOAD                = 0x5002,
    RES_DISK_FULL                   = 0x5003,
    RES_FD_EXHAUSTED                = 0x5004,
    RES_MUTEX_TIMEOUT               = 0x5005,

    // 序列化错误 0x6000-0x6FFF
    SERIALIZE_ENCODE_FAILED         = 0x6001,
    SERIALIZE_DECODE_FAILED         = 0x6002,
    SERIALIZE_VERSION_MISMATCH      = 0x6003,
    SERIALIZE_TYPE_MISMATCH         = 0x6004,

    // 配置错误 0x7000-0x7FFF
    CONFIG_PARSE_FAILED             = 0x7001,
    CONFIG_INVALID_VALUE            = 0x7002,
    CONFIG_MISSING_REQUIRED         = 0x7003,

    // 拓扑/发现错误 0x8000-0x8FFF
    TOPOLOGY_INVALID                = 0x8001,
    TOPOLOGY_CYCLE_DETECTED         = 0x8002,
    TOPOLOGY_TRANSACTION_ALREADY_DONE = 0x8003,
    DISCOVERY_NO_PEERS              = 0x8004,
    DISCOVERY_SUBSCRIPTION_FULL     = 0x8005,

    // 节点生命周期错误 0x9000-0x9FFF
    NODE_INIT_FAILED                = 0x9001,
    NODE_ALREADY_EXISTS             = 0x9002,
    NODE_NOT_FOUND                  = 0x9003,

    // 通用错误 0xF000-0xFFFF
    UNKNOWN                         = 0xFFFF,
    INVALID_ARG                     = 0xFFFE,
    INTERNAL                        = 0xFFFD,
    NOT_IMPLEMENTED                 = 0xFFFC,
    RESOURCE_BUSY                   = 0xFFFB,
};

}  // namespace udaf::core
```

### 2.4 C 接口聚合桶

跨语言绑定（C API）使用聚合桶将细粒度错误码归类，客户端通过 `udaf_last_error_detail()` 获取完整 C++ 码值。

```cpp
constexpr int UDAF_ERR_PROTOCOL  = -100;  // 0x1000-0x1FFF
constexpr int UDAF_ERR_NETWORK   = -101;  // 0x2000-0x2FFF
constexpr int UDAF_ERR_CRYPTO    = -102;  // 0x3000-0x3FFF
constexpr int UDAF_ERR_BUSINESS  = -103;  // 0x4000-0x4FFF
constexpr int UDAF_ERR_RESOURCE  = -104;  // 0x5000-0x5FFF
constexpr int UDAF_ERR_SERIALIZE = -105;  // 0x6000-0x6FFF
constexpr int UDAF_ERR_CONFIG    = -106;  // 0x7000-0x7FFF
constexpr int UDAF_ERR_TOPOLOGY  = -107;  // 0x8000-0x8FFF
constexpr int UDAF_ERR_NODE      = -108;  // 0x9000-0x9FFF
constexpr int UDAF_ERR_UNKNOWN   = -200;
```

### 2.5 CLI 退出码映射

CLI 退出码将 ErrorCode 归类为 13 个退出码，对齐 ADR-010 §3.4。

```cpp
namespace udaf::core {

enum class UDAFExitCode : int {
    OK                = 0,
    GENERAL           = 1,
    INVALID_ARG       = 2,    // CONFIG_* / INVALID_ARG
    NETWORK           = 3,    // NET_*
    AUTH              = 4,    // CRYPTO_* / BIZ_AUTH_UNTRUSTED
    RESOURCE          = 5,    // RES_*
    BUSINESS          = 6,    // BIZ_*
    PROTOCOL          = 7,    // PROTOCOL_*
    SERIALIZE         = 8,    // SERIALIZE_*
    TOPOLOGY          = 9,    // TOPOLOGY_* / DISCOVERY_*
    NODE              = 10,   // NODE_*
    USAGE             = 64,   // CLI 用法错误（BSD sysexits.h EX_USAGE）
    INTERRUPTED       = 130,  // SIGINT（Ctrl+C）
};

UDAFExitCode cli_exit_code(ErrorCode ec) noexcept;

}  // namespace udaf::core
```

### 2.6 国际化错误消息

```cpp
namespace udaf::core {

struct ErrorMessage {
    std::string_view en;
    std::string_view zh_cn;
};

inline constexpr auto kErrorMessages = std::to_array<ErrorMessage>({
    // 通用成功 0x0
    {"success: operation completed successfully",      "成功：操作成功完成"},

    // 协议 0x1xxx（7 条）
    {"protocol: invalid magic number",              "协议：魔数非法"},
    {"protocol: version mismatch",                  "协议：版本不匹配"},
    {"protocol: invalid message type",              "协议：消息类型非法"},
    {"protocol: payload exceeds MTU",               "协议：载荷超过 MTU"},
    {"protocol: checksum mismatch",                 "协议：校验和不匹配"},
    {"protocol: peer rejected our protocol version","协议：对端拒绝协议版本"},
    {"protocol: truncated buffer",                  "协议：缓冲区截断"},

    // 网络 0x2xxx（9 条）
    {"network: operation timed out",                "网络：操作超时"},
    {"network: host unreachable",                   "网络：主机不可达"},
    {"network: connection refused",                 "网络：连接被拒绝"},
    {"network: socket closed",                      "网络：socket 已关闭"},
    {"network: broadcast failed",                   "网络：广播失败"},
    {"network: partition detected",                 "网络：分区检测"},
    {"network: rate limited",                       "网络：速率受限"},
    {"network: send failed",                        "网络：发送失败"},
    {"network: not connected",                      "网络：未连接"},

    // 加密 0x3xxx（12 条）
    {"crypto: HMAC mismatch",                       "加密：HMAC 不匹配"},
    {"crypto: TLS handshake failed",                "加密：TLS 握手失败"},
    {"crypto: PSK mismatch",                        "加密：PSK 不匹配"},
    {"crypto: certificate expired",                 "加密：证书过期"},
    {"crypto: certificate untrusted",               "加密：证书不受信任"},
    {"crypto: certificate chain invalid",           "加密：证书链无效"},
    {"crypto: CA unreachable",                      "加密：CA 不可达"},
    {"crypto: CRL check failed",                    "加密：CRL 检查失败"},
    {"crypto: certificate revoked",                 "加密：证书已吊销"},
    {"crypto: handshake failed",                    "加密：握手失败"},
    {"crypto: nonce reused",                        "加密：nonce 重用"},
    {"crypto: downgrade rejected",                  "加密：降级被拒绝"},

    // 业务 0x4xxx（8 条）
    {"business: command execution failed",          "业务：命令执行失败"},
    {"business: file not found",                    "业务：文件未找到"},
    {"business: file permission denied",            "业务：文件权限拒绝"},
    {"business: shell metachar rejected",           "业务：shell 元字符被拒绝"},
    {"business: service not found",                 "业务：服务未找到"},
    {"business: node not registered",               "业务：节点未注册"},
    {"business: protocol version too old",          "业务：协议版本过旧"},
    {"business: untrusted peer",                    "业务：不可信对端"},

    // 资源 0x5xxx（5 条）
    {"resource: memory exhausted",                  "资源：内存耗尽"},
    {"resource: CPU overload",                      "资源：CPU 过载"},
    {"resource: disk full",                         "资源：磁盘已满"},
    {"resource: FD exhausted",                      "资源：文件描述符耗尽"},
    {"resource: mutex timeout",                     "资源：互斥锁超时"},

    // 序列化 0x6xxx（4 条）
    {"serialize: encode failed",                    "序列化：编码失败"},
    {"serialize: decode failed",                    "序列化：解码失败"},
    {"serialize: version mismatch",                 "序列化：版本不匹配"},
    {"serialize: type mismatch",                    "序列化：类型不匹配"},

    // 配置 0x7xxx（3 条）
    {"config: parse failed",                        "配置：解析失败"},
    {"config: invalid value",                       "配置：无效值"},
    {"config: missing required",                    "配置：缺少必填项"},

    // 拓扑/发现 0x8xxx（5 条）
    {"topology: invalid graph",                     "拓扑：无效图结构"},
    {"topology: cycle detected",                    "拓扑：检测到环"},
    {"topology: transaction already done",          "拓扑：事务已提交或回滚"},
    {"discovery: no peers found",                   "发现：未找到对端"},
    {"discovery: subscription full",                "发现：订阅已满"},

    // 节点 0x9xxx（3 条）
    {"node: init failed",                           "节点：初始化失败"},
    {"node: already exists",                        "节点：已存在"},
    {"node: not found",                             "节点：未找到"},

    // 通用 0xFxxx（5 条）
    {"general: unknown error",                      "通用：未知错误"},
    {"general: invalid argument",                   "通用：无效参数"},
    {"general: internal error",                     "通用：内部错误"},
    {"general: not implemented",                    "通用：未实现"},
    {"general: resource busy",                      "通用：资源忙"},
});

std::string_view error_to_en(ErrorCode ec) noexcept;
std::string_view error_to_zh(ErrorCode ec) noexcept;

}  // namespace udaf::core
```

---

## 3. 约束

1. **ErrorCode 枚举只在本 ADR 定义**，其他文档（02/03/04/05）只引用，不重复定义
2. **新增错误码**必须在对应段内分配下一个可用值，提交前更新本 ADR
3. **命名**统一 `SCREAMING_SNAKE_CASE`，禁止 `kPascalCase` 或其他变体
4. **C 聚合桶**保持与 C++ 枚举段对齐，新增段时同步新增聚合桶

---

## 4. 后果

- **正面**：ErrorCode 变更只需维护一份文档，消除多处定义不一致
- **正面**：其他文档通过 `见 ADR-011 §2.3` 引用，无需维护副本
- **负面**：本 ADR 成为高频引用文档，修改需谨慎评审

---

## 5. 参考

- [`docs/01-requirements.md`](../01-requirements.md) v1.0 §4.4 F-X-01
- [`docs/02-architecture.md`](../02-architecture.md) v2.5 §6.1（引用本 ADR）
- [`docs/03-detailed-design.md`](../03-detailed-design.md) v2.0 §8.1（引用本 ADR）
- [`docs/adr/ADR-010-cli-conventions.md`](ADR-010-cli-conventions.md) §3.4（CLI 退出码）

---

## 附录：变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-08-27 | 初稿：从 02/03 合并 ErrorCode 定义，新增 NET_SEND_FAILED/NET_NOT_CONNECTED/TOPOLOGY_TRANSACTION_ALREADY_DONE |
