// test_audit.cpp - 阶段 E1 单元测试
#include <gtest/gtest.h>

#include "audit/audit.hpp"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

using udaf::audit::ActionType;
using udaf::audit::AuditLogger;

class AuditTmp : public ::testing::Test {
protected:
    fs::path path_;
    void SetUp() override {
        path_ = fs::temp_directory_path() /
                ("udaf_audit_" + std::to_string(::getpid()) + ".log");
        std::error_code ec;
        fs::remove(path_, ec);
    }
    void TearDown() override { std::error_code ec; fs::remove(path_, ec); }
};

TEST_F(AuditTmp, AppendIncrementsSequence) {
    AuditLogger log(path_.string());
    auto r1 = log.append(ActionType::NodeRegister, "n1", "host", "{\"a\":1}");
    auto r2 = log.append(ActionType::NodeUnregister, "n1", "host", "{\"a\":2}");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r1.value(), 1u);
    EXPECT_EQ(r2.value(), 2u);
    EXPECT_EQ(log.sequence(), 2u);
}

TEST_F(AuditTmp, FileExistsAfterAppend) {
    AuditLogger log(path_.string());
    ASSERT_TRUE(log.append(ActionType::PskHandshake, "host", "device", "{}").is_ok());
    EXPECT_TRUE(fs::exists(path_));
    EXPECT_GT(fs::file_size(path_), 0u);
}

TEST_F(AuditTmp, VerifyChainOk) {
    AuditLogger log(path_.string());
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(log.append(ActionType::NodeHeartbeat, "h", "d",
                                "{\"i\":" + std::to_string(i) + "}").is_ok());
    }
    auto v = log.verify_chain();
    ASSERT_TRUE(v.is_ok());
    EXPECT_TRUE(v.value());
}

TEST_F(AuditTmp, ActionNameMapping) {
    EXPECT_STREQ(udaf::audit::action_name(ActionType::NodeRegister), "node_register");
    EXPECT_STREQ(udaf::audit::action_name(ActionType::WhitelistUpdate), "whitelist_update");
    EXPECT_STREQ(udaf::audit::action_name(ActionType::ReplayDetected), "replay_detected");
}

TEST_F(AuditTmp, AllActionNamesMapped) {
    // 全部 19 项枚举映射（覆盖 action_name switch 全分支）
    using udaf::audit::action_name;
    EXPECT_STREQ(action_name(ActionType::NodeRegister),       "node_register");
    EXPECT_STREQ(action_name(ActionType::NodeUnregister),     "node_unregister");
    EXPECT_STREQ(action_name(ActionType::NodeHeartbeat),      "node_heartbeat");
    EXPECT_STREQ(action_name(ActionType::ServicePublish),     "service_publish");
    EXPECT_STREQ(action_name(ActionType::ServiceSubscribe),   "service_subscribe");
    EXPECT_STREQ(action_name(ActionType::TopologyUpdate),     "topology_update");
    EXPECT_STREQ(action_name(ActionType::CrossHostSchedule),  "cross_host_schedule");
    EXPECT_STREQ(action_name(ActionType::PskHandshake),       "psk_handshake");
    EXPECT_STREQ(action_name(ActionType::PkiHandshake),       "pki_handshake");
    EXPECT_STREQ(action_name(ActionType::AuthSuccess),        "auth_success");
    EXPECT_STREQ(action_name(ActionType::AuthFailure),        "auth_failure");
    EXPECT_STREQ(action_name(ActionType::CredentialRotate),   "credential_rotate");
    EXPECT_STREQ(action_name(ActionType::CmdExec),            "cmd_exec");
    EXPECT_STREQ(action_name(ActionType::FileTransfer),       "file_transfer");
    EXPECT_STREQ(action_name(ActionType::ConfigChange),       "config_change");
    EXPECT_STREQ(action_name(ActionType::AuditExport),        "audit_export");
    EXPECT_STREQ(action_name(ActionType::WhitelistUpdate),    "whitelist_update");
    EXPECT_STREQ(action_name(ActionType::RateLimitTriggered), "rate_limit_triggered");
    EXPECT_STREQ(action_name(ActionType::ReplayDetected),     "replay_detected");
}

TEST_F(AuditTmp, AuditEventDefaultConstructor) {
    // 覆盖 AuditEvent 结构体默认构造路径（hpp 字段）
    udaf::audit::AuditEvent ev;
    EXPECT_EQ(ev.sequence, 0u);
    EXPECT_EQ(ev.action, ActionType::NodeHeartbeat);
    EXPECT_EQ(ev.timestamp_ns, 0);
    EXPECT_TRUE(ev.actor.empty());
    EXPECT_TRUE(ev.target.empty());
    EXPECT_TRUE(ev.params_json.empty());
    EXPECT_TRUE(ev.prev_hash.empty());
    EXPECT_TRUE(ev.params_hash.empty());
}

// 性能契约 #27：审计 ≥ 1000 条/秒
TEST_F(AuditTmp, WriteThroughput) {
    AuditLogger log(path_.string());
    constexpr int N = 5000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(log.append(ActionType::NodeHeartbeat, "h", "d",
                                "{\"i\":" + std::to_string(i) + "}").is_ok());
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto rate = (N * 1000) / std::max<long>(ms, 1);
    EXPECT_GT(rate, 1000) << "rate=" << rate << " ops/s";
}

#include <unistd.h>
#include <chrono>
#include <algorithm>