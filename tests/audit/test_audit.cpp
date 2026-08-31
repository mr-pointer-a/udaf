// test_audit.cpp - 阶段 E1 单元测试
#include <gtest/gtest.h>

#include "audit/audit.hpp"
#include "core/error_code.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

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

// ===== 覆盖率补充 =====

// 验证 compute_params_hash：相同输入产生相同 hash（通过 append 间接验证）
TEST_F(AuditTmp, ParamsHashDeterministic) {
    AuditLogger log(path_.string());
    // 两次 append 相同 params → 两条 event 的 params_hash 字段应相同
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"x\":1}").is_ok());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"x\":1}").is_ok());
    std::ifstream in(path_);
    std::string line;
    // 跳过 GENESIS 行
    std::getline(in, line);
    std::vector<std::string> event_hashes;
    while (std::getline(in, line)) {
        // 格式: seq|action|actor|target|ts|prev|params_h|json
        // 按 '|' 分隔，第 7 段（索引 6）是 params_hash
        std::vector<std::string> parts;
        std::string seg;
        std::stringstream ss(line);
        while (std::getline(ss, seg, '|')) parts.push_back(seg);
        ASSERT_EQ(parts.size(), 8u) << "event 行应有 8 段";
        event_hashes.push_back(parts[6]);
    }
    ASSERT_EQ(event_hashes.size(), 2u);
    EXPECT_EQ(event_hashes[0], event_hashes[1])
        << "两次相同 params 应有相同 hash";
}

// 验证 compute_params_hash：不同输入产生不同 hash（间接通过 append）
TEST_F(AuditTmp, ParamsHashDistinct) {
    AuditLogger log(path_.string());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"x\":1}").is_ok());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"x\":2}").is_ok());
    std::ifstream in(path_);
    std::string line;
    std::getline(in, line);  // 跳过 GENESIS
    std::vector<std::string> hashes;
    while (std::getline(in, line)) {
        std::vector<std::string> parts;
        std::string seg;
        std::stringstream ss(line);
        while (std::getline(ss, seg, '|')) parts.push_back(seg);
        ASSERT_EQ(parts.size(), 8u);
        hashes.push_back(parts[6]);
    }
    ASSERT_EQ(hashes.size(), 2u);
    EXPECT_NE(hashes[0], hashes[1]);
}

// verify_chain 失败路径：构造 logger 后不 append 直接删除文件，再 verify
TEST_F(AuditTmp, VerifyChainMissingFileReturnsError) {
    AuditLogger log(path_.string());
    fs::remove(path_);
    auto v = log.verify_chain();
    EXPECT_TRUE(v.is_err());
}

// append 失败路径：path 不可写（目录不存在）
TEST_F(AuditTmp, AppendBadPathReturnsError) {
    AuditLogger log("/nonexistent/dir/audit.log");
    auto r = log.append(ActionType::NodeRegister, "a", "t", "{}");
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INTERNAL);
}

// sequence() 反映多次 append
TEST_F(AuditTmp, SequenceReflectsAppends) {
    AuditLogger log(path_.string());
    EXPECT_EQ(log.sequence(), 0u);
    (void)log.append(ActionType::NodeRegister, "a", "t", "{}");
    EXPECT_EQ(log.sequence(), 1u);
    (void)log.append(ActionType::NodeUnregister, "a", "t", "{}");
    EXPECT_EQ(log.sequence(), 2u);
}

// ===== 覆盖率补充（v0.3.14）=====
// verify_chain 行 185：旧格式（无 GENESIS 前缀）→ PROTOCOL_VERSION_MISMATCH
TEST_F(AuditTmp, VerifyChainLegacyFormatReturnsProtocolMismatch) {
    std::ofstream out(path_);
    out << "no_genesis_prefix|data|line\n";
    out.close();
    AuditLogger log(path_.string());
    auto v = log.verify_chain();
    ASSERT_TRUE(v.is_err());
    EXPECT_EQ(v.error(), udaf::core::ErrorCode::PROTOCOL_VERSION_MISMATCH);
}

// verify_chain 行 195/198：链断裂（prev_hash 与实际不匹配）→ Ok(false)
TEST_F(AuditTmp, VerifyChainBrokenReturnsFalse) {
    AuditLogger log(path_.string());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"k\":1}").is_ok());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{\"k\":2}").is_ok());

    // 手动篡改第二条事件的 prev_hash 字段（第 6 段）
    std::ifstream in(path_);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    in.close();
    ASSERT_GE(lines.size(), 3u);  // GENESIS + 2 events
    // 找第 2 个 event 行的第 6 段（prev_hash）
    std::vector<std::string> parts;
    std::string seg;
    std::stringstream ss(lines[2]);
    while (std::getline(ss, seg, '|')) parts.push_back(seg);
    ASSERT_EQ(parts.size(), 8u);
    parts[5] = std::string(parts[5].size(), '0');  // 替换 prev_hash 为全 0
    // 重写
    std::ofstream out(path_);
    out << lines[0] << '\n';
    out << parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) out << '|' << parts[i];
    out << '\n';
    out.close();

    auto v = log.verify_chain();
    ASSERT_TRUE(v.is_ok());
    EXPECT_FALSE(v.value()) << "链断裂 verify_chain 应返回 Ok(false)";
}

// 覆盖 audit.cpp 行 195：parse_event_line 解析失败（segment 数 != 7）
TEST_F(AuditTmp, VerifyChainCorruptedEventReturnsFalse) {
    AuditLogger log(path_.string());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{}").is_ok());
    // 在文件末尾追加一行损坏的事件（缺少 '|'）
    std::ofstream out(path_, std::ios::app);
    out << "garbled_line_no_pipes_here\n";
    out.close();
    auto v = log.verify_chain();
    ASSERT_TRUE(v.is_ok());
    EXPECT_FALSE(v.value()) << "损坏行应让 verify_chain 返回 Ok(false)";
}

// 覆盖 audit.cpp 行 87-88：parse_event_line 触发 stoull/stoll 异常（segment 含非数字）
TEST_F(AuditTmp, VerifyChainEventWithNonNumericSegmentsReturnsFalse) {
    AuditLogger log(path_.string());
    ASSERT_TRUE(log.append(ActionType::NodeRegister, "a", "t", "{}").is_ok());
    // 追加一行格式正确但字段含非数字字符 → stoull 抛异常 → parse 返回 false → verify 返回 Ok(false)
    std::ofstream out(path_, std::ios::app);
    out << "notanumber|action|a|t|0|prev|phash|{}\n";
    out.close();
    auto v = log.verify_chain();
    ASSERT_TRUE(v.is_ok());
    EXPECT_FALSE(v.value());
}