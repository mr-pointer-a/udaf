// test_error_code.cpp - ErrorCode 枚举与工具函数测试（共 8 用例）
#include "error_code.hpp"

#include <gtest/gtest.h>
#include <set>

using udaf::core::ErrorCode;
using udaf::core::to_string;
using udaf::core::category_of;
using udaf::core::category_prefix;
using udaf::core::is_ok;
using udaf::core::is_err;
using udaf::core::to_int;

TEST(ErrorCode, OkIsZero) {
    EXPECT_EQ(to_int(ErrorCode::OK), 0);
    EXPECT_TRUE(is_ok(ErrorCode::OK));
    EXPECT_FALSE(is_err(ErrorCode::OK));
}

TEST(ErrorCode, AnyNonOkIsErr) {
    EXPECT_FALSE(is_ok(ErrorCode::NET_TIMEOUT));
    EXPECT_TRUE(is_err(ErrorCode::NET_TIMEOUT));
}

TEST(ErrorCode, CountIsSixtyOne) {
    // ADR-011 §2.3：61 条 SCREAMING_SNAKE（不含 OK）
    // 协议 7 + 网络 9 + 加密 12 + 业务 8 + 资源 5 + 序列化 4 + 配置 3 + 拓扑/发现 5 + 节点 3 + 通用 5 = 61
    std::set<ErrorCode> codes;
    codes.insert(ErrorCode::PROTOCOL_INVALID_MAGIC);
    codes.insert(ErrorCode::PROTOCOL_VERSION_MISMATCH);
    codes.insert(ErrorCode::PROTOCOL_INVALID_MSG_TYPE);
    codes.insert(ErrorCode::PROTOCOL_PAYLOAD_TOO_LARGE);
    codes.insert(ErrorCode::PROTOCOL_CHECKSUM_MISMATCH);
    codes.insert(ErrorCode::PROTOCOL_PROTOCOL_REJECTED);
    codes.insert(ErrorCode::PROTOCOL_TRUNCATED_BUFFER);

    codes.insert(ErrorCode::NET_TIMEOUT);
    codes.insert(ErrorCode::NET_HOST_UNREACHABLE);
    codes.insert(ErrorCode::NET_CONNECTION_REFUSED);
    codes.insert(ErrorCode::NET_SOCKET_CLOSED);
    codes.insert(ErrorCode::NET_BROADCAST_FAILED);
    codes.insert(ErrorCode::NET_PARTITION_DETECTED);
    codes.insert(ErrorCode::NET_RATE_LIMITED);
    codes.insert(ErrorCode::NET_SEND_FAILED);
    codes.insert(ErrorCode::NET_NOT_CONNECTED);

    codes.insert(ErrorCode::CRYPTO_HMAC_MISMATCH);
    codes.insert(ErrorCode::CRYPTO_TLS_HANDSHAKE_FAILED);
    codes.insert(ErrorCode::CRYPTO_PSK_MISMATCH);
    codes.insert(ErrorCode::CRYPTO_CERT_EXPIRED);
    codes.insert(ErrorCode::CRYPTO_CERT_UNTRUSTED);
    codes.insert(ErrorCode::CRYPTO_CERT_CHAIN_INVALID);
    codes.insert(ErrorCode::CRYPTO_CA_UNREACHABLE);
    codes.insert(ErrorCode::CRYPTO_CRL_CHECK_FAILED);
    codes.insert(ErrorCode::CRYPTO_CERT_REVOKED);
    codes.insert(ErrorCode::CRYPTO_HANDSHAKE_FAILED);
    codes.insert(ErrorCode::CRYPTO_NONCE_REUSED);
    codes.insert(ErrorCode::CRYPTO_DOWNGRADE_REJECTED);

    codes.insert(ErrorCode::BIZ_CMD_EXEC_FAILED);
    codes.insert(ErrorCode::BIZ_FILE_NOT_FOUND);
    codes.insert(ErrorCode::BIZ_FILE_PERMISSION_DENIED);
    codes.insert(ErrorCode::BIZ_SHELL_METACHAR_REJECTED);
    codes.insert(ErrorCode::BIZ_SERVICE_NOT_FOUND);
    codes.insert(ErrorCode::BIZ_NODE_NOT_REGISTERED);
    codes.insert(ErrorCode::BIZ_PROTOCOL_VERSION_TOO_OLD);
    codes.insert(ErrorCode::BIZ_AUTH_UNTRUSTED);

    codes.insert(ErrorCode::RES_MEMORY_EXHAUSTED);
    codes.insert(ErrorCode::RES_CPU_OVERLOAD);
    codes.insert(ErrorCode::RES_DISK_FULL);
    codes.insert(ErrorCode::RES_FD_EXHAUSTED);
    codes.insert(ErrorCode::RES_MUTEX_TIMEOUT);

    codes.insert(ErrorCode::SERIALIZE_ENCODE_FAILED);
    codes.insert(ErrorCode::SERIALIZE_DECODE_FAILED);
    codes.insert(ErrorCode::SERIALIZE_VERSION_MISMATCH);
    codes.insert(ErrorCode::SERIALIZE_TYPE_MISMATCH);

    codes.insert(ErrorCode::CONFIG_PARSE_FAILED);
    codes.insert(ErrorCode::CONFIG_INVALID_VALUE);
    codes.insert(ErrorCode::CONFIG_MISSING_REQUIRED);

    codes.insert(ErrorCode::TOPOLOGY_INVALID);
    codes.insert(ErrorCode::TOPOLOGY_CYCLE_DETECTED);
    codes.insert(ErrorCode::TOPOLOGY_TRANSACTION_ALREADY_DONE);
    codes.insert(ErrorCode::DISCOVERY_NO_PEERS);
    codes.insert(ErrorCode::DISCOVERY_SUBSCRIPTION_FULL);

    codes.insert(ErrorCode::NODE_INIT_FAILED);
    codes.insert(ErrorCode::NODE_ALREADY_EXISTS);
    codes.insert(ErrorCode::NODE_NOT_FOUND);

    codes.insert(ErrorCode::RESOURCE_BUSY);
    codes.insert(ErrorCode::NOT_IMPLEMENTED);
    codes.insert(ErrorCode::INTERNAL);
    codes.insert(ErrorCode::INVALID_ARG);
    codes.insert(ErrorCode::UNKNOWN);

    EXPECT_EQ(codes.size(), 61u);
}

TEST(ErrorCode, CategoryPrefixIsCategoryHigh16) {
    using U = std::uint32_t;
    EXPECT_EQ(category_prefix(ErrorCode::NET_TIMEOUT),        U{0x00002000});
    EXPECT_EQ(category_prefix(ErrorCode::CRYPTO_HMAC_MISMATCH), U{0x00003000});
    EXPECT_EQ(category_prefix(ErrorCode::UNKNOWN),            U{0x0000F000});
    EXPECT_EQ(category_prefix(ErrorCode::OK),                 U{0x00000000});
    EXPECT_EQ(category_prefix(ErrorCode::TOPOLOGY_CYCLE_DETECTED), U{0x00008000});
    EXPECT_EQ(category_prefix(ErrorCode::DISCOVERY_NO_PEERS),    U{0x00008000});
}

TEST(ErrorCode, CategoryOfGroupsByPrefix) {
    using U = std::uint32_t;
    EXPECT_STREQ(category_of(ErrorCode::PROTOCOL_INVALID_MAGIC), "PROTOCOL");
    EXPECT_STREQ(category_of(ErrorCode::NET_TIMEOUT), "NET");
    EXPECT_STREQ(category_of(ErrorCode::CRYPTO_HMAC_MISMATCH), "CRYPTO");
    EXPECT_STREQ(category_of(ErrorCode::BIZ_CMD_EXEC_FAILED), "BIZ");
    EXPECT_STREQ(category_of(ErrorCode::RES_MEMORY_EXHAUSTED), "RES");
    EXPECT_STREQ(category_of(ErrorCode::SERIALIZE_DECODE_FAILED), "SERIALIZE");
    EXPECT_STREQ(category_of(ErrorCode::CONFIG_PARSE_FAILED), "CONFIG");
    EXPECT_STREQ(category_of(ErrorCode::TOPOLOGY_CYCLE_DETECTED), "TOPOLOGY");
    EXPECT_STREQ(category_of(ErrorCode::DISCOVERY_NO_PEERS), "TOPOLOGY");
    EXPECT_STREQ(category_of(ErrorCode::NODE_INIT_FAILED), "NODE");
    EXPECT_STREQ(category_of(ErrorCode::UNKNOWN), "GENERAL");
    (void)U{};  // 占位避免 unused 警告
}

TEST(ErrorCode, ToStringReturnsNonNull) {
    EXPECT_STREQ(to_string(ErrorCode::OK), "OK");
    EXPECT_STREQ(to_string(ErrorCode::NET_TIMEOUT), "network timeout");
    EXPECT_STREQ(to_string(ErrorCode::UNKNOWN), "unknown error");
}

TEST(ErrorCode, ToStringOnUnknownValueIsSafe) {
    // 不在枚举中的值：to_string 应不抛异常，返回 fallback 字符串
    ErrorCode invalid = static_cast<ErrorCode>(0xDEAD);
    const char* s = to_string(invalid);
    EXPECT_NE(s, nullptr);
}

TEST(ErrorCode, ValuesAreUnique) {
    std::set<std::uint32_t> values;
    auto insert = [&](ErrorCode c) {
        EXPECT_TRUE(values.insert(static_cast<std::uint32_t>(c)).second)
            << "duplicate ErrorCode value: 0x"
            << std::hex << static_cast<std::uint32_t>(c);
    };
    insert(ErrorCode::PROTOCOL_INVALID_MAGIC);
    insert(ErrorCode::NET_TIMEOUT);
    insert(ErrorCode::CRYPTO_HMAC_MISMATCH);
    insert(ErrorCode::BIZ_CMD_EXEC_FAILED);
    insert(ErrorCode::RES_MEMORY_EXHAUSTED);
    insert(ErrorCode::SERIALIZE_ENCODE_FAILED);
    insert(ErrorCode::CONFIG_PARSE_FAILED);
    insert(ErrorCode::TOPOLOGY_INVALID);
    insert(ErrorCode::DISCOVERY_NO_PEERS);
    insert(ErrorCode::NODE_INIT_FAILED);
    insert(ErrorCode::UNKNOWN);
    insert(ErrorCode::INVALID_ARG);
    insert(ErrorCode::INTERNAL);
    insert(ErrorCode::NOT_IMPLEMENTED);
    insert(ErrorCode::RESOURCE_BUSY);
}

// ===== 全 61 条 ErrorCode 的 to_string 覆盖 =====
TEST(ErrorCode, ToStringAllCodes) {
    using E = ErrorCode;
    // 调用 to_string 确保每个 case 分支都被命中
    EXPECT_STREQ(to_string(E::PROTOCOL_INVALID_MAGIC),      "protocol invalid magic");
    EXPECT_STREQ(to_string(E::PROTOCOL_VERSION_MISMATCH),    "protocol version mismatch");
    EXPECT_STREQ(to_string(E::PROTOCOL_INVALID_MSG_TYPE),   "protocol invalid message type");
    EXPECT_STREQ(to_string(E::PROTOCOL_PAYLOAD_TOO_LARGE),   "protocol payload too large");
    EXPECT_STREQ(to_string(E::PROTOCOL_CHECKSUM_MISMATCH),   "protocol checksum mismatch");
    EXPECT_STREQ(to_string(E::PROTOCOL_PROTOCOL_REJECTED),   "protocol rejected");
    EXPECT_STREQ(to_string(E::PROTOCOL_TRUNCATED_BUFFER),    "protocol truncated buffer");

    EXPECT_STREQ(to_string(E::NET_TIMEOUT),                  "network timeout");
    EXPECT_STREQ(to_string(E::NET_HOST_UNREACHABLE),         "network host unreachable");
    EXPECT_STREQ(to_string(E::NET_CONNECTION_REFUSED),       "network connection refused");
    EXPECT_STREQ(to_string(E::NET_SOCKET_CLOSED),            "network socket closed");
    EXPECT_STREQ(to_string(E::NET_BROADCAST_FAILED),         "network broadcast failed");
    EXPECT_STREQ(to_string(E::NET_PARTITION_DETECTED),       "network partition detected");
    EXPECT_STREQ(to_string(E::NET_RATE_LIMITED),             "network rate limited");
    EXPECT_STREQ(to_string(E::NET_SEND_FAILED),              "network send failed");
    EXPECT_STREQ(to_string(E::NET_NOT_CONNECTED),            "network not connected");

    EXPECT_STREQ(to_string(E::CRYPTO_HMAC_MISMATCH),         "crypto HMAC mismatch");
    EXPECT_STREQ(to_string(E::CRYPTO_TLS_HANDSHAKE_FAILED),  "crypto TLS handshake failed");
    EXPECT_STREQ(to_string(E::CRYPTO_PSK_MISMATCH),          "crypto PSK mismatch");
    EXPECT_STREQ(to_string(E::CRYPTO_CERT_EXPIRED),          "crypto certificate expired");
    EXPECT_STREQ(to_string(E::CRYPTO_CERT_UNTRUSTED),        "crypto certificate untrusted");
    EXPECT_STREQ(to_string(E::CRYPTO_CERT_CHAIN_INVALID),    "crypto certificate chain invalid");
    EXPECT_STREQ(to_string(E::CRYPTO_CA_UNREACHABLE),        "crypto CA unreachable");
    EXPECT_STREQ(to_string(E::CRYPTO_CRL_CHECK_FAILED),      "crypto CRL check failed");
    EXPECT_STREQ(to_string(E::CRYPTO_CERT_REVOKED),          "crypto certificate revoked");
    EXPECT_STREQ(to_string(E::CRYPTO_HANDSHAKE_FAILED),      "crypto handshake failed");
    EXPECT_STREQ(to_string(E::CRYPTO_NONCE_REUSED),          "crypto nonce reused");
    EXPECT_STREQ(to_string(E::CRYPTO_DOWNGRADE_REJECTED),    "crypto downgrade rejected");

    EXPECT_STREQ(to_string(E::BIZ_CMD_EXEC_FAILED),          "business command execution failed");
    EXPECT_STREQ(to_string(E::BIZ_FILE_NOT_FOUND),           "business file not found");
    EXPECT_STREQ(to_string(E::BIZ_FILE_PERMISSION_DENIED),   "business file permission denied");
    EXPECT_STREQ(to_string(E::BIZ_SHELL_METACHAR_REJECTED),  "business shell metachar rejected");
    EXPECT_STREQ(to_string(E::BIZ_SERVICE_NOT_FOUND),        "business service not found");
    EXPECT_STREQ(to_string(E::BIZ_NODE_NOT_REGISTERED),      "business node not registered");
    EXPECT_STREQ(to_string(E::BIZ_PROTOCOL_VERSION_TOO_OLD), "business protocol version too old");
    EXPECT_STREQ(to_string(E::BIZ_AUTH_UNTRUSTED),           "business auth untrusted");

    EXPECT_STREQ(to_string(E::RES_MEMORY_EXHAUSTED),         "resource memory exhausted");
    EXPECT_STREQ(to_string(E::RES_CPU_OVERLOAD),             "resource CPU overload");
    EXPECT_STREQ(to_string(E::RES_DISK_FULL),                "resource disk full");
    EXPECT_STREQ(to_string(E::RES_FD_EXHAUSTED),             "resource fd exhausted");
    EXPECT_STREQ(to_string(E::RES_MUTEX_TIMEOUT),            "resource mutex timeout");

    EXPECT_STREQ(to_string(E::SERIALIZE_ENCODE_FAILED),      "serialize encode failed");
    EXPECT_STREQ(to_string(E::SERIALIZE_DECODE_FAILED),      "serialize decode failed");
    EXPECT_STREQ(to_string(E::SERIALIZE_VERSION_MISMATCH),   "serialize version mismatch");
    EXPECT_STREQ(to_string(E::SERIALIZE_TYPE_MISMATCH),      "serialize type mismatch");

    EXPECT_STREQ(to_string(E::CONFIG_PARSE_FAILED),          "config parse failed");
    EXPECT_STREQ(to_string(E::CONFIG_INVALID_VALUE),         "config invalid value");
    EXPECT_STREQ(to_string(E::CONFIG_MISSING_REQUIRED),      "config missing required");

    EXPECT_STREQ(to_string(E::TOPOLOGY_INVALID),             "topology invalid");
    EXPECT_STREQ(to_string(E::TOPOLOGY_CYCLE_DETECTED),      "topology cycle detected");
    EXPECT_STREQ(to_string(E::TOPOLOGY_TRANSACTION_ALREADY_DONE), "topology transaction already done");
    EXPECT_STREQ(to_string(E::DISCOVERY_NO_PEERS),           "discovery no peers");
    EXPECT_STREQ(to_string(E::DISCOVERY_SUBSCRIPTION_FULL),  "discovery subscription full");

    EXPECT_STREQ(to_string(E::NODE_INIT_FAILED),             "node init failed");
    EXPECT_STREQ(to_string(E::NODE_ALREADY_EXISTS),          "node already exists");
    EXPECT_STREQ(to_string(E::NODE_NOT_FOUND),               "node not found");

    EXPECT_STREQ(to_string(E::RESOURCE_BUSY),                "resource busy");
    EXPECT_STREQ(to_string(E::NOT_IMPLEMENTED),              "not implemented");
    EXPECT_STREQ(to_string(E::INTERNAL),                     "internal error");
    EXPECT_STREQ(to_string(E::INVALID_ARG),                  "invalid argument");
}