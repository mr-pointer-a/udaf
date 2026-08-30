// error_code.cpp - ErrorCode 字符串映射实现
#include "error_code.hpp"

namespace udaf::core {

const char* to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::OK: return "OK";

    case ErrorCode::PROTOCOL_INVALID_MAGIC: return "protocol invalid magic";
    case ErrorCode::PROTOCOL_VERSION_MISMATCH: return "protocol version mismatch";
    case ErrorCode::PROTOCOL_INVALID_MSG_TYPE: return "protocol invalid message type";
    case ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE: return "protocol payload too large";
    case ErrorCode::PROTOCOL_CHECKSUM_MISMATCH: return "protocol checksum mismatch";
    case ErrorCode::PROTOCOL_PROTOCOL_REJECTED: return "protocol rejected";
    case ErrorCode::PROTOCOL_TRUNCATED_BUFFER: return "protocol truncated buffer";

    case ErrorCode::NET_TIMEOUT: return "network timeout";
    case ErrorCode::NET_HOST_UNREACHABLE: return "network host unreachable";
    case ErrorCode::NET_CONNECTION_REFUSED: return "network connection refused";
    case ErrorCode::NET_SOCKET_CLOSED: return "network socket closed";
    case ErrorCode::NET_BROADCAST_FAILED: return "network broadcast failed";
    case ErrorCode::NET_PARTITION_DETECTED: return "network partition detected";
    case ErrorCode::NET_RATE_LIMITED: return "network rate limited";
    case ErrorCode::NET_SEND_FAILED: return "network send failed";
    case ErrorCode::NET_NOT_CONNECTED: return "network not connected";

    case ErrorCode::CRYPTO_HMAC_MISMATCH: return "crypto HMAC mismatch";
    case ErrorCode::CRYPTO_TLS_HANDSHAKE_FAILED: return "crypto TLS handshake failed";
    case ErrorCode::CRYPTO_PSK_MISMATCH: return "crypto PSK mismatch";
    case ErrorCode::CRYPTO_CERT_EXPIRED: return "crypto certificate expired";
    case ErrorCode::CRYPTO_CERT_UNTRUSTED: return "crypto certificate untrusted";
    case ErrorCode::CRYPTO_CERT_CHAIN_INVALID: return "crypto certificate chain invalid";
    case ErrorCode::CRYPTO_CA_UNREACHABLE: return "crypto CA unreachable";
    case ErrorCode::CRYPTO_CRL_CHECK_FAILED: return "crypto CRL check failed";
    case ErrorCode::CRYPTO_CERT_REVOKED: return "crypto certificate revoked";
    case ErrorCode::CRYPTO_HANDSHAKE_FAILED: return "crypto handshake failed";
    case ErrorCode::CRYPTO_NONCE_REUSED: return "crypto nonce reused";
    case ErrorCode::CRYPTO_DOWNGRADE_REJECTED: return "crypto downgrade rejected";

    case ErrorCode::BIZ_CMD_EXEC_FAILED: return "business command execution failed";
    case ErrorCode::BIZ_FILE_NOT_FOUND: return "business file not found";
    case ErrorCode::BIZ_FILE_PERMISSION_DENIED: return "business file permission denied";
    case ErrorCode::BIZ_SHELL_METACHAR_REJECTED: return "business shell metachar rejected";
    case ErrorCode::BIZ_SERVICE_NOT_FOUND: return "business service not found";
    case ErrorCode::BIZ_NODE_NOT_REGISTERED: return "business node not registered";
    case ErrorCode::BIZ_PROTOCOL_VERSION_TOO_OLD: return "business protocol version too old";
    case ErrorCode::BIZ_AUTH_UNTRUSTED: return "business auth untrusted";

    case ErrorCode::RES_MEMORY_EXHAUSTED: return "resource memory exhausted";
    case ErrorCode::RES_CPU_OVERLOAD: return "resource CPU overload";
    case ErrorCode::RES_DISK_FULL: return "resource disk full";
    case ErrorCode::RES_FD_EXHAUSTED: return "resource fd exhausted";
    case ErrorCode::RES_MUTEX_TIMEOUT: return "resource mutex timeout";

    case ErrorCode::SERIALIZE_ENCODE_FAILED: return "serialize encode failed";
    case ErrorCode::SERIALIZE_DECODE_FAILED: return "serialize decode failed";
    case ErrorCode::SERIALIZE_VERSION_MISMATCH: return "serialize version mismatch";
    case ErrorCode::SERIALIZE_TYPE_MISMATCH: return "serialize type mismatch";

    case ErrorCode::CONFIG_PARSE_FAILED: return "config parse failed";
    case ErrorCode::CONFIG_INVALID_VALUE: return "config invalid value";
    case ErrorCode::CONFIG_MISSING_REQUIRED: return "config missing required";

    case ErrorCode::TOPOLOGY_INVALID: return "topology invalid";
    case ErrorCode::TOPOLOGY_CYCLE_DETECTED: return "topology cycle detected";
    case ErrorCode::TOPOLOGY_TRANSACTION_ALREADY_DONE: return "topology transaction already done";
    case ErrorCode::DISCOVERY_NO_PEERS: return "discovery no peers";
    case ErrorCode::DISCOVERY_SUBSCRIPTION_FULL: return "discovery subscription full";

    case ErrorCode::NODE_INIT_FAILED: return "node init failed";
    case ErrorCode::NODE_ALREADY_EXISTS: return "node already exists";
    case ErrorCode::NODE_NOT_FOUND: return "node not found";

    case ErrorCode::RESOURCE_BUSY: return "resource busy";
    case ErrorCode::NOT_IMPLEMENTED: return "not implemented";
    case ErrorCode::INTERNAL: return "internal error";
    case ErrorCode::INVALID_ARG: return "invalid argument";
    case ErrorCode::UNKNOWN: return "unknown error";
    }
    return "unknown error code";
}

const char* category_of(ErrorCode code) noexcept {
    const uint32_t prefix = category_prefix(code);
    switch (prefix) {
    case 0x00000000U: return "OK";
    case 0x00001000U: return "PROTOCOL";
    case 0x00002000U: return "NET";
    case 0x00003000U: return "CRYPTO";
    case 0x00004000U: return "BIZ";
    case 0x00005000U: return "RES";
    case 0x00006000U: return "SERIALIZE";
    case 0x00007000U: return "CONFIG";
    case 0x00008000U: return "TOPOLOGY";  // 含 DISCOVERY
    case 0x00009000U: return "NODE";
    case 0x0000F000U: return "GENERAL";
    default: return "UNKNOWN";
    }
}

}  // namespace udaf::core