// test_sdk.cpp - Client 完整功能测试
#include <gtest/gtest.h>

#include "sdk/sdk/sdk.hpp"


#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

using udaf::sdk::Client;
using udaf::sdk::ClientConfig;
using udaf::core::ErrorCode;
using udaf::core::Result;
using udaf::audit::ActionType;

namespace {

class SdkTest : public ::testing::Test {
protected:
    fs::path audit_path_;
    ClientConfig default_cfg() {
        ClientConfig c;
        c.node_id      = "host-test";
        c.bind_address = "127.0.0.1";
        c.bind_port    = 9999;
        c.audit_path   = audit_path_.string();
        return c;
    }
    void SetUp() override {
        audit_path_ = fs::temp_directory_path() /
            ("udaf_sdk_" + std::to_string(::getpid()) + ".log");
        std::error_code ec; fs::remove(audit_path_, ec);
    }
    void TearDown() override {
        std::error_code ec; fs::remove(audit_path_, ec);
    }
};

// 一个 64 字符（32 字节）合法 hex fingerprint
std::string valid_fp() {
    return "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
}

}  // namespace

// ---------- 构造 / 启动 / 停止 ----------

TEST_F(SdkTest, ConstructWithAudit) {
    Client c(default_cfg());
    EXPECT_EQ(c.sequence(), 0u);
}

TEST_F(SdkTest, ConstructWithoutAudit) {
    ClientConfig cfg;
    cfg.node_id = "h";
    Client c(cfg);
    EXPECT_EQ(c.sequence(), 0u);
}

TEST_F(SdkTest, StartStop) {
    Client c(default_cfg());
    EXPECT_TRUE(c.start().is_ok());
    EXPECT_GT(c.sequence(), 0u);
    EXPECT_TRUE(c.stop().is_ok());
}

TEST_F(SdkTest, AuditAppendsToSequence) {
    Client c(default_cfg());
    auto r1 = c.audit(ActionType::NodeHeartbeat, "t1", "{}");
    auto r2 = c.audit(ActionType::NodeHeartbeat, "t2", "{}");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r1.value(), 1u);
    EXPECT_EQ(r2.value(), 2u);
}

TEST_F(SdkTest, AuditFailsWithoutLogger) {
    ClientConfig cfg;
    cfg.node_id = "h";
    Client c(cfg);
    auto r = c.audit(ActionType::NodeRegister, "t", "{}");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, SequenceNoLogger) {
    ClientConfig cfg;
    cfg.node_id = "h";
    Client c(cfg);
    EXPECT_EQ(c.sequence(), 0u);
}

// ---------- discover / topology_summary / list_nodes ----------

TEST_F(SdkTest, DiscoverEmpty) {
    Client c(default_cfg());
    auto nodes = c.discover("");
    EXPECT_TRUE(nodes.empty());
}

TEST_F(SdkTest, DiscoverWithFilter) {
    Client c(default_cfg());
    c.register_node("n1", "h1", "127.0.0.1", 8000);
    c.register_node("n2", "h2", "127.0.0.1", 8001);
    auto nodes = c.discover("any");
    EXPECT_EQ(nodes.size(), 2u);
}

TEST_F(SdkTest, TopologySummary) {
    Client c(default_cfg());
    c.register_node("n1", "h1", "127.0.0.1", 8000);
    c.register_node("n2", "h2", "127.0.0.1", 8001);
    auto s = c.topology_summary();
    EXPECT_EQ(s.node_count, 2u);
    EXPECT_EQ(s.nodes.size(), 2u);
}

TEST_F(SdkTest, TopologySummaryEmpty) {
    Client c(default_cfg());
    auto s = c.topology_summary();
    EXPECT_EQ(s.node_count, 0u);
    EXPECT_TRUE(s.nodes.empty());
}

TEST_F(SdkTest, ListNodes) {
    Client c(default_cfg());
    c.register_node("alpha", "h", "127.0.0.1", 1);
    c.register_node("beta",  "h", "127.0.0.1", 2);
    auto ns = c.list_nodes();
    EXPECT_EQ(ns.size(), 2u);
}

// ---------- register_node / unregister_node ----------

TEST_F(SdkTest, RegisterAndUnregister) {
    Client c(default_cfg());
    EXPECT_TRUE(c.register_node("n1", "h1", "127.0.0.1", 8000).is_ok());
    EXPECT_EQ(c.list_nodes().size(), 1u);
    EXPECT_TRUE(c.unregister_node("n1").is_ok());
    EXPECT_EQ(c.list_nodes().size(), 0u);
}

TEST_F(SdkTest, UnregisterNonexistentReturnsFalse) {
    Client c(default_cfg());
    auto r = c.unregister_node("ghost");
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

// ---------- push_file / pull_file ----------

TEST_F(SdkTest, PushFileFailsWithoutLogger) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    auto r = c.push_file("/src", "dst", "/dst");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, PushFileEmptyArgReturnsErr) {
    Client c(default_cfg());
    auto r1 = c.push_file("",      "dst", "/d");
    auto r2 = c.push_file("/s",    "",    "/d");
    auto r3 = c.push_file("/s",    "dst", "");
    EXPECT_TRUE(r1.is_err());
    EXPECT_TRUE(r2.is_err());
    EXPECT_TRUE(r3.is_err());
    EXPECT_EQ(r1.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, PushFileUntrustedReturnsErr) {
    Client c(default_cfg());
    auto r = c.push_file("/src", "untrusted", "/dst");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::BIZ_AUTH_UNTRUSTED);
}

TEST_F(SdkTest, PushFileTrustedOk) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {"file_xfer"});
    auto r = c.push_file("/src", "d1", "/dst");
    EXPECT_TRUE(r.is_ok());
}

TEST_F(SdkTest, PullFileFailsWithoutLogger) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    auto r = c.pull_file("src", "/p", "/d");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, PullFileUntrustedReturnsErr) {
    Client c(default_cfg());
    auto r = c.pull_file("ghost", "/p", "/d");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::BIZ_AUTH_UNTRUSTED);
}

TEST_F(SdkTest, PullFileTrustedOk) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.pull_file("d1", "/p", "/d");
    EXPECT_TRUE(r.is_ok());
}

// ---------- run_remote ----------

TEST_F(SdkTest, RunRemoteUntrustedReturnsErr) {
    Client c(default_cfg());
    auto r = c.run_remote("ghost", "/bin/true", {});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::BIZ_AUTH_UNTRUSTED);
}

TEST_F(SdkTest, RunRemoteEmptyCmdReturnsErr) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.run_remote("d1", "", {});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, RunRemoteWithoutLogger) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    c.trust_add("d1", valid_fp(), {});
    auto r = c.run_remote("d1", "echo", {"hello"});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, RunRemoteOk) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.run_remote("d1", "/bin/echo", {"hello", "world"});
    EXPECT_TRUE(r.is_ok());
}

TEST_F(SdkTest, RunRemoteNoArgs) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.run_remote("d1", "/bin/true", {});
    EXPECT_TRUE(r.is_ok());
}

// ---------- trust_list / trust_add / trust_remove ----------

TEST_F(SdkTest, TrustListEmpty) {
    Client c(default_cfg());
    auto t = c.trust_list();
    EXPECT_TRUE(t.empty());
}

TEST_F(SdkTest, TrustAddInvalidFpReturnsErr) {
    Client c(default_cfg());
    auto r = c.trust_add("n1", "tooshort", {});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, TrustAddInvalidHexCharReturnsErr) {
    Client c(default_cfg());
    auto r = c.trust_add("n1", "zz" + std::string(62, '0'), {});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, TrustAddAndRemove) {
    Client c(default_cfg());
    EXPECT_TRUE(c.trust_add("n1", valid_fp(), {"cmd-exec", "file_xfer"}).is_ok());
    EXPECT_TRUE(c.trust_remove("n1").is_ok());
    EXPECT_FALSE(c.trust_remove("n1").is_ok());  // 第二次返回 false → 转 NODE_NOT_FOUND
}

TEST_F(SdkTest, TrustRemoveNonexistentReturnsErr) {
    Client c(default_cfg());
    auto r = c.trust_remove("ghost");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::NODE_NOT_FOUND);
}

// ---------- psk_rotate / auth_psk ----------

TEST_F(SdkTest, PskRotateNoLoggerReturnsErr) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    auto r = c.psk_rotate("/new.psk");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, PskRotateEmptyReturnsErr) {
    Client c(default_cfg());
    auto r = c.psk_rotate("");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, PskRotateOk) {
    Client c(default_cfg());
    auto r = c.psk_rotate("/tmp/new.psk");
    EXPECT_TRUE(r.is_ok());
}

TEST_F(SdkTest, AuthPskNoLoggerReturnsErr) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    auto r = c.auth_psk("d1");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, AuthPskUntrustedReturnsErr) {
    Client c(default_cfg());
    auto r = c.auth_psk("ghost");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::BIZ_AUTH_UNTRUSTED);
}

TEST_F(SdkTest, AuthPskOk) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.auth_psk("d1");
    EXPECT_TRUE(r.is_ok());
}

// ---------- migrate ----------

TEST_F(SdkTest, MigrateNoLoggerReturnsErr) {
    ClientConfig cfg; cfg.node_id = "h";
    Client c(cfg);
    auto r = c.migrate("/src", "/dst");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST_F(SdkTest, MigrateEmptyReturnsErr) {
    Client c(default_cfg());
    auto r1 = c.migrate("", "/dst");
    auto r2 = c.migrate("/src", "");
    EXPECT_TRUE(r1.is_err());
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r1.error(), ErrorCode::INVALID_ARG);
}

TEST_F(SdkTest, MigrateOk) {
    Client c(default_cfg());
    auto r = c.migrate("/a/audit.log", "/b/audit.log");
    EXPECT_TRUE(r.is_ok());
}

// ---------- config_show ----------

TEST_F(SdkTest, ConfigShow) {
    Client c(default_cfg());
    c.register_node("n1", "h1", "127.0.0.1", 8000);
    c.trust_add("d1", valid_fp(), {});
    auto s = c.config_show();
    EXPECT_NE(s.find("host-test"),    std::string::npos);
    EXPECT_NE(s.find("127.0.0.1"),    std::string::npos);
    EXPECT_NE(s.find("9999"),         std::string::npos);
    EXPECT_NE(s.find("nodes=1"),      std::string::npos);
    EXPECT_NE(s.find("trust_entries=1"), std::string::npos);
}

TEST_F(SdkTest, ConfigShowEmpty) {
    Client c(default_cfg());
    auto s = c.config_show();
    EXPECT_NE(s.find("nodes=0"),        std::string::npos);
    EXPECT_NE(s.find("trust_entries=0"), std::string::npos);
}
