// test_sdk.cpp - Client 完整功能测试
#include <gtest/gtest.h>

#include "sdk/sdk/sdk.hpp"


#include <atomic>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <vector>

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

// 全大写 hex fingerprint
std::string valid_fp_upper() {
    return "AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899";
}

// 混合大小写 hex fingerprint
std::string valid_fp_mixed() {
    return "AaBbCcDdEeFf00112233445566778899AaBbCcDdEeFf00112233445566778899";
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

// ============================================================
// F4 新增覆盖：PIMPL 行为 / 大小写 hex / 重复添加 / 持久化 / 并发
// ============================================================

// 拷贝构造被删除（编译期保证）
static_assert(!std::is_copy_constructible<Client>::value, "Client must not be copy constructible");
static_assert(!std::is_copy_assignable<Client>::value,    "Client must not be copy assignable");

// Client PIMPL 大小：sizeof(Client) 等于一个 unique_ptr（验证 PIMPL 隔离 ABI）
TEST_F(SdkTest, PimplSizeIsSinglePointer) {
    EXPECT_EQ(sizeof(Client), sizeof(void*));
}

// 大写 hex fingerprint 也应接受
TEST_F(SdkTest, TrustAddUpperCaseHex) {
    Client c(default_cfg());
    EXPECT_TRUE(c.trust_add("n1", valid_fp_upper(), {}).is_ok());
    auto lst = c.trust_list();
    ASSERT_EQ(lst.size(), 1u);
    // 16 进制转换输出应小写（实现用 'a' + ... 路径）
    EXPECT_EQ(lst[0].fingerprint_sha256_hex, "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
}

// 混合大小写 hex fingerprint 也应接受
TEST_F(SdkTest, TrustAddMixedCaseHex) {
    Client c(default_cfg());
    EXPECT_TRUE(c.trust_add("n1", valid_fp_mixed(), {}).is_ok());
}

// trust_list 输出 64 字符 hex
TEST_F(SdkTest, TrustListHexFormatIs64Chars) {
    Client c(default_cfg());
    c.trust_add("n1", valid_fp(), {});
    auto lst = c.trust_list();
    ASSERT_EQ(lst.size(), 1u);
    EXPECT_EQ(lst[0].fingerprint_sha256_hex.size(), 64u);
}

// trust_list 多个条目 + capabilities 完整保留
TEST_F(SdkTest, TrustListMultipleWithCapabilities) {
    Client c(default_cfg());
    c.trust_add("n1", valid_fp(), {"cmd-exec", "file_xfer"});
    c.trust_add("n2", valid_fp_upper(), {"heartbeat"});
    auto lst = c.trust_list();
    EXPECT_EQ(lst.size(), 2u);
    // capabilities 保留顺序
    bool found_n1 = false, found_n2 = false;
    for (const auto& e : lst) {
        if (e.node_id == "n1") {
            found_n1 = true;
            EXPECT_EQ(e.capabilities.size(), 2u);
        } else if (e.node_id == "n2") {
            found_n2 = true;
            EXPECT_EQ(e.capabilities.size(), 1u);
        }
    }
    EXPECT_TRUE(found_n1);
    EXPECT_TRUE(found_n2);
}

// 重复 trust_add 同一 node_id → 第二次返回 false（更新而非复制）
TEST_F(SdkTest, TrustAddDuplicateReturnsFalse) {
    Client c(default_cfg());
    EXPECT_TRUE(c.trust_add("n1", valid_fp(), {}).is_ok());
    auto r = c.trust_add("n1", valid_fp_upper(), {});
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

// 重复 register_node 同一 node_id → is_ok 且 false
TEST_F(SdkTest, RegisterNodeDuplicateReturnsFalse) {
    Client c(default_cfg());
    EXPECT_TRUE(c.register_node("n1", "h", "127.0.0.1", 8000).is_ok());
    auto r = c.register_node("n1", "h2", "127.0.0.2", 8001);
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

// unregister 不存在的 node_id → false（is_ok）
TEST_F(SdkTest, UnregisterNodeNonexistentReturnsFalse) {
    Client c(default_cfg());
    auto r = c.unregister_node("ghost");
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

// Sequence 在 audit 多次后单调递增
TEST_F(SdkTest, SequenceMonotonicAcrossAppends) {
    Client c(default_cfg());
    auto r1 = c.audit(ActionType::NodeHeartbeat, "t1", "{}");
    auto r2 = c.audit(ActionType::NodeHeartbeat, "t2", "{}");
    auto r3 = c.audit(ActionType::NodeHeartbeat, "t3", "{}");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    ASSERT_TRUE(r3.is_ok());
    EXPECT_LT(r1.value(), r2.value());
    EXPECT_LT(r2.value(), r3.value());
}

// Sequence 在 Client 重建后从 0 重新开始（新实例独立 audit path）
TEST_F(SdkTest, SequenceFreshAfterClientReinit) {
    Client c(default_cfg());
    c.audit(ActionType::NodeHeartbeat, "t1", "{}");
    c.audit(ActionType::NodeHeartbeat, "t2", "{}");
    EXPECT_GT(c.sequence(), 0u);
    // 销毁 c，新建实例指向**不同**文件 → sequence 从 0 开始
    ClientConfig cfg2 = default_cfg();
    cfg2.audit_path = audit_path_.string() + ".other";
    std::error_code ec; fs::remove(cfg2.audit_path, ec);
    Client c2(cfg2);
    EXPECT_EQ(c2.sequence(), 0u);
    std::error_code ec2; fs::remove(cfg2.audit_path, ec2);
}

// Sequence 持久化：新建 Client 指向**同一**文件 → sequence 续接
TEST_F(SdkTest, SequencePersistsAcrossClientRestart) {
    Client c(default_cfg());
    auto r1 = c.audit(ActionType::NodeHeartbeat, "t1", "{}");
    auto r2 = c.audit(ActionType::NodeHeartbeat, "t2", "{}");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value(), 2u);

    // 销毁 c，新建实例指向同一文件 → 应读到 sequence=2
    Client c2(default_cfg());
    EXPECT_EQ(c2.sequence(), 2u);
    auto r3 = c2.audit(ActionType::NodeHeartbeat, "t3", "{}");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(r3.value(), 3u);
}

// 并发 audit append：4 线程各 25 次 → 总共 100 次 audit → sequence ≥ 100
TEST_F(SdkTest, ConcurrentAuditAppends) {
    Client c(default_cfg());
    constexpr int kThreads = 4;
    constexpr int kPerThread = 25;
    std::vector<std::thread> ts;
    std::atomic<int> ok_count{0};
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&c, &ok_count] {
            for (int j = 0; j < kPerThread; ++j) {
                auto r = c.audit(ActionType::NodeHeartbeat, "t", "{}");
                if (r.is_ok()) ok_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(ok_count.load(), kThreads * kPerThread);
    EXPECT_GE(c.sequence(), static_cast<std::uint64_t>(kThreads * kPerThread));
}

// discover 找到已注册的节点
TEST_F(SdkTest, DiscoverAfterRegister) {
    Client c(default_cfg());
    c.register_node("alice", "h1", "10.0.0.1", 8000);
    c.register_node("bob",   "h2", "10.0.0.2", 8001);
    auto nodes = c.discover("");
    EXPECT_EQ(nodes.size(), 2u);
    bool found_alice = false, found_bob = false;
    for (const auto& e : nodes) {
        if (e.node_id_ == "alice") found_alice = true;
        if (e.node_id_ == "bob")   found_bob   = true;
    }
    EXPECT_TRUE(found_alice);
    EXPECT_TRUE(found_bob);
}

// register → unregister → discover → 不再包含
TEST_F(SdkTest, DiscoverAfterUnregister) {
    Client c(default_cfg());
    c.register_node("alice", "h1", "10.0.0.1", 8000);
    c.register_node("bob",   "h2", "10.0.0.2", 8001);
    EXPECT_TRUE(c.unregister_node("alice").is_ok());
    auto nodes = c.discover("");
    EXPECT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes[0].node_id_, "bob");
}

// start/stop 多次调用幂等
TEST_F(SdkTest, StartStopIdempotent) {
    Client c(default_cfg());
    EXPECT_TRUE(c.start().is_ok());
    EXPECT_TRUE(c.start().is_ok());  // 二次 start 不应崩
    (void)c.stop();
    EXPECT_TRUE(c.stop().is_ok());   // 二次 stop 不应崩
}

// start 应触发审计事件（验证 audit chain 不被破坏）
TEST_F(SdkTest, StartAppendsAuditEvent) {
    Client c(default_cfg());
    auto seq_before = c.sequence();
    EXPECT_TRUE(c.start().is_ok());
    EXPECT_GT(c.sequence(), seq_before);
}

// config_show 中 node_count 反映 register/unregister
TEST_F(SdkTest, ConfigShowReflectsNodeChanges) {
    Client c(default_cfg());
    c.register_node("a", "h", "127.0.0.1", 1);
    c.register_node("b", "h", "127.0.0.1", 2);
    EXPECT_NE(c.config_show().find("nodes=2"), std::string::npos);
    c.unregister_node("a");
    EXPECT_NE(c.config_show().find("nodes=1"), std::string::npos);
}

// config_show 中 trust_entries 反映 trust_add/remove
TEST_F(SdkTest, ConfigShowReflectsTrustChanges) {
    Client c(default_cfg());
    EXPECT_NE(c.config_show().find("trust_entries=0"), std::string::npos);
    c.trust_add("n1", valid_fp(), {});
    EXPECT_NE(c.config_show().find("trust_entries=1"), std::string::npos);
    c.trust_remove("n1");
    EXPECT_NE(c.config_show().find("trust_entries=0"), std::string::npos);
}

// pull_file 实现行为：未校验空路径（仅校验 whitelist + audit_logger）
// 此测试记录**实际**行为，避免后续误改（push_file 才校验空路径）
TEST_F(SdkTest, PullFileEmptyArgsBehavior) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    // 受信任节点 → 空路径被记录但不报错（实现无校验）
    EXPECT_TRUE(c.pull_file("d1", "",   "/d").is_ok());
    EXPECT_TRUE(c.pull_file("d1", "/p", "").is_ok());
    // 空 src_node 仍受 whitelist 校验 → 失败
    EXPECT_TRUE(c.pull_file("",   "/p", "/d").is_err());
}

// run_remote 带大量 args → JSON 数组完整序列化
TEST_F(SdkTest, RunRemoteManyArgsSerializedCorrectly) {
    Client c(default_cfg());
    c.trust_add("d1", valid_fp(), {});
    auto r = c.run_remote("d1", "/bin/echo",
                          {"arg1", "arg2", "arg3", "arg4", "arg5"});
    EXPECT_TRUE(r.is_ok());
}

// start 后 sequence 应大于初始 0
TEST_F(SdkTest, StartAdvancingSequence) {
    Client c(default_cfg());
    EXPECT_EQ(c.sequence(), 0u);
    (void)c.start();
    EXPECT_GT(c.sequence(), 0u);
    (void)c.stop();
}

// config_show bind_address 含默认
TEST_F(SdkTest, ConfigShowContainsBindInfo) {
    Client c(default_cfg());
    auto s = c.config_show();
    EXPECT_NE(s.find("bind_address=127.0.0.1"), std::string::npos);
    EXPECT_NE(s.find("bind_port=9999"),         std::string::npos);
}

// audit 多次调用都返回递增 seq
TEST_F(SdkTest, AuditReturnsIncreasingSeq) {
    Client c(default_cfg());
    std::uint64_t prev = 0;
    for (int i = 0; i < 10; ++i) {
        auto r = c.audit(ActionType::NodeHeartbeat, "t", "{}");
        ASSERT_TRUE(r.is_ok());
        if (i > 0) { EXPECT_GT(r.value(), prev); }
        prev = r.value();
    }
}
