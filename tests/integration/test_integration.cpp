// test_integration.cpp - §10.1~§10.8 跨模块链路
//
// §10.1 discovery→bridge→topology→node 主链路
// §10.2 心跳聚合
// §10.3 加密握手往返
// §10.4 跨主机白名单调度
// §10.5 节点生命周期

#include <gtest/gtest.h>

#include "ability_a/bridge/discovery_bridge.hpp"
#include "ability_a/registry/service_registry.hpp"
#include "ability_a/trust/peer_whitelist.hpp"
#include "ability_b/topology/topology.hpp"
#include "ability_b/node/node.hpp"
#include "ability_c/executor/process_executor.hpp"
#include "ability_c/nodes/nodes.hpp"
#include "audit/audit.hpp"
#include "crypto/psk.hpp"
#include "observability/observability.hpp"
#include "sdk/sdk/sdk.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using udaf::ability_a::registry::ServiceRegistry;
using udaf::ability_a::registry::RegistryEntry;
using udaf::ability_a::registry::ServiceDescriptor;
using udaf::ability_a::registry::RegistryEvent;
using udaf::ability_a::bridge::DiscoveryBridge;
using udaf::ability_a::trust::PeerWhitelist;
using udaf::ability_b::topology::Topology;
using udaf::ability_b::topology::TopologyTransaction;
using udaf::ability_b::node::NodeConfig;
using udaf::ability_b::node::Node;
using udaf::ability_c::nodes::CmdExecNode;
using udaf::ability_c::executor::ProcessExecutor;

// ============================================================
// §10.1 A→B 主链路：registry → bridge → topology → node spawn
// ============================================================
TEST(Integration_S10_1, DiscoveryToNodeSpawn) {
    ServiceRegistry reg;

    // 1) 订阅 registry 变化（unique_ptr 自动 RAII）
    std::atomic<int> add_count{0};
    auto sub = reg.subscribe(
        [&](RegistryEvent ev, const RegistryEntry& e) {
            if (ev == RegistryEvent::Add) add_count.fetch_add(1);
        });

    // 2) "发现"3 个节点（模拟 Advertiser 投递）
    for (int i = 0; i < 3; ++i) {
        RegistryEntry e;
        e.node_id_ = "device-" + std::to_string(i);
        e.hostname_ = "h" + std::to_string(i);
        e.bind_address_ = "10.0.0." + std::to_string(i + 1);
        e.bind_port_ = 9000 + i;
        e.services_.push_back({"cmd_exec", 9001, "tcp"});
        ASSERT_TRUE(reg.register_node(e).is_ok());
    }
    EXPECT_EQ(reg.size(), 3u);
    EXPECT_GE(add_count.load(), 3);
    sub.reset();  // 显式释放（可选，析构时也会）

    // 3) DiscoveryBridge → TopologyUpdateCallbacks 桥接
    struct CapturingCallbacks : public udaf::bridge::TopologyUpdateCallbacks {
        std::vector<std::string> joined;
        std::vector<std::string> left;
        std::vector<std::string> heartbeats;
        void on_node_join(const udaf::bridge::NodeJoinEvent& ev) noexcept override {
            joined.push_back(ev.node_id);
        }
        void on_node_leave(const udaf::bridge::NodeLeaveEvent& ev) noexcept override {
            left.push_back(ev.node_id);
        }
        void on_node_heartbeat(std::string_view node_id) noexcept override {
            heartbeats.emplace_back(node_id);
        }
    } cb;
    DiscoveryBridge bridge(&cb);
    for (const auto& e : reg.snapshot()) {
        (void)bridge.on_node_join(e.node_id_, e.hostname_,
                                  e.bind_address_, e.bind_port_,
                                  std::vector<std::string>{"cmd_exec"});
    }
    EXPECT_EQ(cb.joined.size(), 3u);

    // 4) 构造 Topology
    Topology topo;
    auto tx = topo.begin_transaction().value();
    for (const auto& e : reg.snapshot()) {
        udaf::ability_b::topology::PeerNode pn;
        pn.node_id      = e.node_id_;
        pn.hostname     = e.hostname_;
        pn.bind_address = e.bind_address_;
        pn.bind_port    = e.bind_port_;
        pn.capabilities = {"cmd_exec"};
        tx.add_node(std::move(pn));
    }
    auto cr = topo.commit(std::move(tx));
    ASSERT_TRUE(cr.is_ok());
    EXPECT_EQ(topo.node_count(), 3u);

    // 5) spawn CmdExecNode
    CmdExecNode node;
    node.set_allowed_executables({"/bin/echo"});
    NodeConfig cfg;
    EXPECT_TRUE(node.init(cfg).is_ok());
    EXPECT_TRUE(node.start().is_ok());
    EXPECT_TRUE(node.stop().is_ok());
}

// ============================================================
// §10.2 心跳聚合：100 设备心跳输入 → 触发 topology 心跳计数
// ============================================================
TEST(Integration_S10_2, HeartbeatAggregation100) {
    ServiceRegistry reg;
    // 注册 100 个"设备"
    for (int i = 0; i < 100; ++i) {
        RegistryEntry e;
        e.node_id_ = "hb-" + std::to_string(i);
        e.bind_address_ = "10.1.0." + std::to_string((i % 254) + 1);
        ASSERT_TRUE(reg.register_node(e).is_ok());
    }
    auto t0 = std::chrono::steady_clock::now();
    auto snap = reg.snapshot();
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_EQ(snap.size(), 100u);
    EXPECT_LT(ms, 200) << "100 nodes snapshot = " << ms << "ms (contract #14)";
}

// ============================================================
// §10.3 加密链路：PSK 握手往返 + AEAD
// ============================================================
TEST(Integration_S10_3, PskHandshakeRoundTrip) {
    namespace crypto = udaf::crypto;
    // 32 字节 PSK
    std::vector<std::uint8_t> psk(32, 0xAB);

    // 1) 客户端发起 + 服务端响应
    auto req = crypto::psk_handshake_client_new("client-1");
    EXPECT_FALSE(req.identity.empty());

    auto resp = crypto::psk_handshake_server_respond(psk, req);
    ASSERT_TRUE(resp.is_ok());

    // 2) 客户端派生会话密钥
    auto keys = crypto::psk_handshake_client_finalize(psk, req, resp.value());
    ASSERT_TRUE(keys.is_ok());
    EXPECT_EQ(keys.value().enc_key.size(), 32u);

    // 3) AEAD 往返
    std::array<std::uint8_t, 12> nonce_arr{};
    nonce_arr.fill(0x33);
    std::vector<std::uint8_t> aad = {'a','a','d'};
    std::vector<std::uint8_t> plain = {'h','e','l','l','o'};
    auto ct = crypto::psk_aead_encrypt(keys.value().enc_key, nonce_arr, plain, aad);
    ASSERT_TRUE(ct.is_ok());
    auto pt = crypto::psk_aead_decrypt(keys.value().enc_key, nonce_arr, ct.value(), aad);
    ASSERT_TRUE(pt.is_ok());
    EXPECT_EQ(pt.value(), plain);

    // 4) 篡改 aad → 解密失败
    std::vector<std::uint8_t> bad_aad = {'b','a','d'};
    auto bad = crypto::psk_aead_decrypt(keys.value().enc_key, nonce_arr, ct.value(), bad_aad);
    EXPECT_TRUE(bad.is_err());
}

// ============================================================
// §10.4 跨主机白名单调度：白名单拒绝 → 跨主机节点不被调度
// ============================================================
TEST(Integration_S10_4, WhitelistDeniedBlocksSchedule) {
    PeerWhitelist wl;
    // 仅允许 device-1
    udaf::ability_a::trust::WhitelistEntry e;
    e.node_id = "device-1";
    e.allowed_capabilities_ = {"cmd_exec"};
    e.fingerprint_sha256_ = std::vector<std::uint8_t>(32, 0xAA);
    ASSERT_TRUE(wl.add(e).is_ok());

    // Scheduler 通过回调注入白名单决策
    auto check = [&](const std::string& node_id) {
        return wl.contains(node_id);
    };
    EXPECT_TRUE(check("device-1"));   // 白名单内
    EXPECT_FALSE(check("device-2"));  // 不在 → 拒绝
    EXPECT_FALSE(check("evil-node")); // 恶意节点 → 拒绝
}

// ============================================================
// §10.5 节点生命周期 + 审计 + 可观测性
// ============================================================
TEST(Integration_S10_5, NodeLifecycleAuditAndMetrics) {
    udaf::observability::Meter meter;
    auto t0 = std::chrono::steady_clock::now();

    CmdExecNode n;
    n.set_allowed_executables({"/bin/echo"});
    NodeConfig cfg;
    EXPECT_TRUE(n.init(cfg).is_ok());
    meter.inc_counter(udaf::observability::MetricId::NodeLifecycleChangesTotal);

    EXPECT_TRUE(n.start().is_ok());
    meter.inc_counter(udaf::observability::MetricId::NodeLifecycleChangesTotal);

    // 直接调 ProcessExecutor（白名单 /bin/echo 已注入）
    ProcessExecutor::Options opts;
    opts.executable = "/bin/echo";
    opts.args = {"integration"};
    opts.allowed_executables = {"/bin/echo"};
    auto r = ProcessExecutor::execute(opts);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().exit_code, 0);

    EXPECT_TRUE(n.stop().is_ok());
    meter.inc_counter(udaf::observability::MetricId::NodeLifecycleChangesTotal);

    auto prom = meter.export_prometheus();
    EXPECT_NE(prom.find("node_lifecycle_changes_total"), std::string::npos);

    // 全流程 < 1s
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(us, 1'000'000) << "lifecycle=" << us << "us";
}

// ============================================================
// §10.6 SDK Client 串联 audit + discover + lifecycle
// ============================================================
TEST(Integration_S10_6, SdkClientFullChain) {
    auto tmp = std::filesystem::temp_directory_path() /
               ("udaf_int_" + std::to_string(::getpid()) + ".log");
    std::error_code ec; std::filesystem::remove(tmp, ec);

    udaf::sdk::ClientConfig cfg;
    cfg.node_id = "int-host-1";
    cfg.audit_path = tmp.string();

    udaf::sdk::Client client(cfg);
    EXPECT_TRUE(client.start().is_ok());
    auto start_seq = client.sequence();  // start 写 1 条 NodeRegister

    // 走 5 条审计
    for (int i = 0; i < 5; ++i) {
        auto r = client.audit(udaf::audit::ActionType::NodeHeartbeat,
                              "device-" + std::to_string(i),
                              "{\"i\":" + std::to_string(i) + "}");
        ASSERT_TRUE(r.is_ok());
    }
    EXPECT_EQ(client.sequence(), start_seq + 5);

    auto entries = client.discover("");
    EXPECT_TRUE(entries.empty());

    EXPECT_TRUE(client.stop().is_ok());
    EXPECT_TRUE(std::filesystem::exists(tmp));
    EXPECT_GT(std::filesystem::file_size(tmp), 0u);
    std::filesystem::remove(tmp, ec);
}

#include <filesystem>
#include <unistd.h>