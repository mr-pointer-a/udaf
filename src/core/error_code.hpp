// error_code.hpp - UDAF 错误码枚举（单一权威源，移植自 ADR-011 §2.3）
//
// 类别划分：
//   PROTOCOL_*  0x1000-0x1FFF   协议错误
//   NET_*       0x2000-0x2FFF   网络错误
//   CRYPTO_*    0x3000-0x3FFF   加密错误
//   BIZ_*       0x4000-0x4FFF   业务错误
//   RES_*       0x5000-0x5FFF   资源耗尽
//   SERIALIZE_* 0x6000-0x6FFF   序列化错误
//   CONFIG_*    0x7000-0x7FFF   配置错误
//   TOPOLOGY_*  0x8000-0x8FFF   拓扑/发现错误
//   DISCOVERY_* 0x8000-0x8FFF   （同上段）
//   NODE_*      0x9000-0x9FFF   节点生命周期错误
//   通用        0xFFFB-0xFFFF   通用错误
//
// 共 61 条 SCREAMING_SNAKE 错误码 + OK = 0
//
// 权威源：docs/adr/ADR-011-error-codes.md

#ifndef UDAF_CORE_ERROR_CODE_HPP
#define UDAF_CORE_ERROR_CODE_HPP

#include <cstdint>

namespace udaf::core {

/// UDAF 错误码枚举。所有 Result<T> / ErrorCode 路径使用此类型。
/// 禁止自造错误码，必须从此处引用。
enum class ErrorCode : uint16_t {
    OK = 0,

    // ---------- 协议错误 0x1000-0x1FFF ----------
    PROTOCOL_INVALID_MAGIC = 0x1001,
    PROTOCOL_VERSION_MISMATCH = 0x1002,
    PROTOCOL_INVALID_MSG_TYPE = 0x1003,
    PROTOCOL_PAYLOAD_TOO_LARGE = 0x1004,
    PROTOCOL_CHECKSUM_MISMATCH = 0x1005,
    PROTOCOL_PROTOCOL_REJECTED = 0x1006,
    PROTOCOL_TRUNCATED_BUFFER = 0x1007,

    // ---------- 网络错误 0x2000-0x2FFF ----------
    NET_TIMEOUT = 0x2001,
    NET_HOST_UNREACHABLE = 0x2002,
    NET_CONNECTION_REFUSED = 0x2003,
    NET_SOCKET_CLOSED = 0x2004,
    NET_BROADCAST_FAILED = 0x2005,
    NET_PARTITION_DETECTED = 0x2006,
    NET_RATE_LIMITED = 0x2007,
    NET_SEND_FAILED = 0x2008,
    NET_NOT_CONNECTED = 0x2009,

    // ---------- 加密错误 0x3000-0x3FFF ----------
    CRYPTO_HMAC_MISMATCH = 0x3001,
    CRYPTO_TLS_HANDSHAKE_FAILED = 0x3002,
    CRYPTO_PSK_MISMATCH = 0x3003,
    CRYPTO_CERT_EXPIRED = 0x3004,
    CRYPTO_CERT_UNTRUSTED = 0x3005,
    CRYPTO_CERT_CHAIN_INVALID = 0x3006,
    CRYPTO_CA_UNREACHABLE = 0x3007,
    CRYPTO_CRL_CHECK_FAILED = 0x3008,
    CRYPTO_CERT_REVOKED = 0x3009,
    CRYPTO_HANDSHAKE_FAILED = 0x300A,
    CRYPTO_NONCE_REUSED = 0x300B,
    CRYPTO_DOWNGRADE_REJECTED = 0x300C,

    // ---------- 业务错误 0x4000-0x4FFF ----------
    BIZ_CMD_EXEC_FAILED = 0x4001,
    BIZ_FILE_NOT_FOUND = 0x4002,
    BIZ_FILE_PERMISSION_DENIED = 0x4003,
    BIZ_SHELL_METACHAR_REJECTED = 0x4004,
    BIZ_SERVICE_NOT_FOUND = 0x4005,
    BIZ_NODE_NOT_REGISTERED = 0x4006,
    BIZ_PROTOCOL_VERSION_TOO_OLD = 0x4007,
    BIZ_AUTH_UNTRUSTED = 0x4008,

    // ---------- 资源耗尽 0x5000-0x5FFF ----------
    RES_MEMORY_EXHAUSTED = 0x5001,
    RES_CPU_OVERLOAD = 0x5002,
    RES_DISK_FULL = 0x5003,
    RES_FD_EXHAUSTED = 0x5004,
    RES_MUTEX_TIMEOUT = 0x5005,

    // ---------- 序列化错误 0x6000-0x6FFF ----------
    SERIALIZE_ENCODE_FAILED = 0x6001,
    SERIALIZE_DECODE_FAILED = 0x6002,
    SERIALIZE_VERSION_MISMATCH = 0x6003,
    SERIALIZE_TYPE_MISMATCH = 0x6004,

    // ---------- 配置错误 0x7000-0x7FFF ----------
    CONFIG_PARSE_FAILED = 0x7001,
    CONFIG_INVALID_VALUE = 0x7002,
    CONFIG_MISSING_REQUIRED = 0x7003,

    // ---------- 拓扑/发现错误 0x8000-0x8FFF ----------
    TOPOLOGY_INVALID = 0x8001,
    TOPOLOGY_CYCLE_DETECTED = 0x8002,
    TOPOLOGY_TRANSACTION_ALREADY_DONE = 0x8003,
    DISCOVERY_NO_PEERS = 0x8004,
    DISCOVERY_SUBSCRIPTION_FULL = 0x8005,

    // ---------- 节点生命周期错误 0x9000-0x9FFF ----------
    NODE_INIT_FAILED = 0x9001,
    NODE_ALREADY_EXISTS = 0x9002,
    NODE_NOT_FOUND = 0x9003,

    // ---------- 通用错误 0xFFFB-0xFFFF ----------
    RESOURCE_BUSY = 0xFFFB,
    NOT_IMPLEMENTED = 0xFFFC,
    INTERNAL = 0xFFFD,
    INVALID_ARG = 0xFFFE,
    UNKNOWN = 0xFFFF,
};

/// 将 ErrorCode 转换为 C 风格 int（用于 C API / 日志输出）。
/// OK → 0；其他错误码按原值返回。
[[nodiscard]] constexpr int to_int(ErrorCode code) noexcept {
    return static_cast<int>(code);
}

/// 检查是否为 OK。
[[nodiscard]] constexpr bool is_ok(ErrorCode code) noexcept {
    return code == ErrorCode::OK;
}

/// 检查是否为错误（非 OK）。
[[nodiscard]] constexpr bool is_err(ErrorCode code) noexcept {
    return code != ErrorCode::OK;
}

/// 返回错误码的可读字符串（仅用于日志/调试，不用于协议字段）。
/// 此函数故意不使用 i18n 表，避免引入额外依赖（详见 ADR-011 §2.5）。
[[nodiscard]] const char* to_string(ErrorCode code) noexcept;

/// 返回错误码的类别字符串（PROTOCOL / NET / CRYPTO / ...）。
/// 用于按类别聚合统计。
[[nodiscard]] const char* category_of(ErrorCode code) noexcept;

/// 返回错误码的高 4 位（即类别前缀，0x1=PROTOCOL, 0x2=NET, ... 0xF=GENERAL）。
/// 类别由单个十六进制字符编码（参考 ADR-011 §2.3），故掩码为 0xF000。
[[nodiscard]] constexpr uint32_t category_prefix(ErrorCode code) noexcept {
    return static_cast<uint32_t>(code) & 0x0000F000U;
}

}  // namespace udaf::core

#endif  // UDAF_CORE_ERROR_CODE_HPP