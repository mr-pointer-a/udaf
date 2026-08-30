// test_ability_a.cpp - 阶段 B2/B3/B4 单元测试
//
// 测试用例：
//   B3: ServiceRegistry register/query/update/remove
//   B3: SubscriptionHandle RAII
//   B3: 10000 条性能契约 #14
//   B2: UdpSocket 基础收发
//   B2: 单播速率限制 5/s
//   B2: PSK 加密往返
//   B4: TopologyUpdateCallbacks 注入

#include <gtest/gtest.h>

#include "ability_a/registry/service_registry.hpp"
#include "ability_a/transport/udp_socket.hpp"
#include "ability_a/discovery/advertisement.hpp"
#include "ability_a/trust/peer_whitelist.hpp"
#include "ability_a/bridge/discovery_bridge.hpp"
#include "bridge/topology_update_callbacks.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

using udaf::ability_a::registry::RegistryEntry;
using udaf::ability_a::registry::RegistryEvent;
using udaf::ability_a::registry::ServiceDescriptor;
using udaf::ability_a::registry::ServiceRegistry;
using udaf::ability_a::transport::Endpoint;
using udaf::ability_a::transport::UdpSocket;
using udaf::bridge::NodeJoinEvent;
using udaf::bridge::NodeLeaveEvent;
using udaf::bridge::TopologyUpdateCallbacks;
using udaf::core::ErrorCode;

namespace {

RegistryEntry make_entry(std::string id, std::uint16_t port = 8080) {
    RegistryEntry e;
    e.node_id_ = std::move(id);
    e.hostname_ = "host-" + e.node_id_;
    e.bind_address_ = "127.0.0.1";
    e.bind_port_ = port;
    e.services_.push_back(ServiceDescriptor{"cmd-exec", 9000, "tcp"});
    return e;
}

}  // namespace

// ---------- B3: ServiceRegistry ----------

TEST(UdafRegistry, RegisterAndQuery) {
    ServiceRegistry reg;
    auto r = reg.register_node(make_entry("n1"));
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());  // 新增

    auto q = reg.get_node("n1");
    ASSERT_TRUE(q.is_ok());
    EXPECT_EQ(q.value().node_id_, "n1");
    EXPECT_EQ(q.value().services_.size(), 1u);
}

TEST(UdafRegistry, UpdateExistingNode) {
    ServiceRegistry reg;
    EXPECT_TRUE(reg.register_node(make_entry("n1", 1)).value());
    EXPECT_FALSE(reg.register_node(make_entry("n1", 2)).value());  // update
    EXPECT_EQ(reg.get_node("n1").value().bind_port_, 2);
}

TEST(UdafRegistry, UnregisterNode) {
    ServiceRegistry reg;
    ASSERT_TRUE(reg.register_node(make_entry("n1")).is_ok());
    auto r = reg.unregister_node("n1");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());
    EXPECT_TRUE(reg.get_node("n1").is_err());
}

TEST(UdafRegistry, SubscribeFiresOnChange) {
    ServiceRegistry reg;
    std::atomic<int> add_count{0}, upd_count{0}, rm_count{0};
    auto handle = reg.subscribe([&](RegistryEvent ev, const RegistryEntry&) {
        switch (ev) {
            case RegistryEvent::Add:    ++add_count; break;
            case RegistryEvent::Update: ++upd_count; break;
            case RegistryEvent::Remove: ++rm_count; break;
        }
    });
    ASSERT_NE(handle, nullptr);
    reg.register_node(make_entry("n1"));
    reg.register_node(make_entry("n1", 9));  // update
    reg.unregister_node("n1");
    EXPECT_EQ(add_count.load(), 1);
    EXPECT_EQ(upd_count.load(), 1);
    EXPECT_EQ(rm_count.load(), 1);
}

TEST(UdafRegistry, SubscriptionHandleRAII) {
    ServiceRegistry reg;
    std::atomic<int> cnt{0};
    {
        auto h = reg.subscribe([&](RegistryEvent, const RegistryEntry&) {
            ++cnt;
        });
        ASSERT_NE(h, nullptr);
        EXPECT_TRUE(h->valid());
    }
    (void)reg.register_node(make_entry("n1"));
    EXPECT_EQ(cnt.load(), 0) << "RAII 后订阅应自动取消";
}

TEST(UdafRegistry, MoveSubscriptionHandle) {
    ServiceRegistry reg;
    std::atomic<int> cnt{0};
    auto h = reg.subscribe([&](RegistryEvent, const RegistryEntry&) {
        ++cnt;
    });
    ASSERT_NE(h, nullptr);
    auto id = h->id();
    auto h2 = std::move(h);
    EXPECT_EQ(h2->id(), id);
    EXPECT_EQ(h.get(), nullptr);
}

TEST(UdafRegistry, SubscriptionHandleMoveAssign) {
    // 覆盖 SubscriptionHandle 移动赋值（service_registry.cpp 行 17-25）
    ServiceRegistry reg;
    std::atomic<int> cnt{0};
    auto h1 = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++cnt; });
    auto h2 = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++cnt; });
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);
    auto id1 = h1->id();
    auto id2 = h2->id();
    ASSERT_NE(id1, id2);
    // h1 = std::move(h2) → h1 应释放原订阅，接管 h2
    h1 = std::move(h2);
    EXPECT_EQ(h1->id(), id2);
    EXPECT_EQ(h2.get(), nullptr);
    // 触发事件 → 只有 h1 对应的回调（id2）应被通知
    RegistryEntry e; e.node_id_ = "n1";
    reg.register_node(e);
    EXPECT_EQ(cnt.load(), 1);
}

TEST(UdafRegistry, SubscriptionHandleMoveSelfAssign) {
    // 覆盖 service_registry.cpp 行 18 自赋值检查
    ServiceRegistry reg;
    auto h = reg.subscribe([](RegistryEvent, const RegistryEntry&) {});
    ASSERT_NE(h, nullptr);
    auto id = h->id();
    h = std::move(h);  // 自赋值
    EXPECT_EQ(h->id(), id);  // 仍有效
}

TEST(UdafRegistry, Capacity10000) {
    ServiceRegistry reg;
    for (int i = 0; i < 10000; ++i) {
        ASSERT_TRUE(reg.register_node(make_entry("node-" + std::to_string(i))).is_ok());
    }
    EXPECT_EQ(reg.size(), 10000u);
    auto t0 = std::chrono::steady_clock::now();
    auto snap = reg.snapshot();
    auto t1 = std::chrono::steady_clock::now();
    EXPECT_EQ(snap.size(), 10000u);
    auto ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    EXPECT_LT(ms, 100.0) << "snapshot 耗时 " << ms << "ms (契约 #14: <100ms)";
}

// ---------- B4: TopologyUpdateCallbacks ----------

namespace {

class TestCallbacks : public TopologyUpdateCallbacks {
public:
    std::atomic<int> joins{0}, leaves{0};
    void on_node_join(const NodeJoinEvent& /*ev*/) noexcept override { ++joins; }
    void on_node_leave(const NodeLeaveEvent& /*ev*/) noexcept override { ++leaves; }
};

}  // namespace

TEST(UdafBridge, TopologyUpdateCallbacks) {
    TestCallbacks cb;
    NodeJoinEvent je; je.node_id = "n1"; je.bind_port = 8080;
    NodeLeaveEvent le; le.node_id = "n1";
    cb.on_node_join(je);
    cb.on_node_leave(le);
    EXPECT_EQ(cb.joins.load(), 1);
    EXPECT_EQ(cb.leaves.load(), 1);
}

// ---------- DiscoveryBridge ----------

using udaf::ability_a::bridge::DiscoveryBridge;

namespace {

class BridgeTestCB : public TopologyUpdateCallbacks {
public:
    std::atomic<int> joins{0}, leaves{0}, hbs{0};
    std::string last_node_id;
    void on_node_join(const NodeJoinEvent& ev) noexcept override {
        ++joins;
        last_node_id = ev.node_id;
    }
    void on_node_leave(const NodeLeaveEvent& ev) noexcept override {
        ++leaves;
        last_node_id = ev.node_id;
    }
    void on_node_heartbeat(std::string_view node_id) noexcept override {
        ++hbs;
        last_node_id = std::string(node_id);
    }
};

}  // namespace

TEST(UdafDiscoveryBridge, OnJoinForwards) {
    BridgeTestCB cb;
    DiscoveryBridge bridge(&cb);
    EXPECT_TRUE(bridge.has_callbacks());
    std::vector<std::string> svcs{"cmd", "file"};
    auto r = bridge.on_node_join("node-x", "host-x", "127.0.0.1", 9000, svcs);
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(cb.joins.load(), 1);
    EXPECT_EQ(cb.last_node_id, "node-x");
}

TEST(UdafDiscoveryBridge, OnLeaveForwards) {
    BridgeTestCB cb;
    DiscoveryBridge bridge(&cb);
    auto r = bridge.on_node_leave("node-y");
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(cb.leaves.load(), 1);
    EXPECT_EQ(cb.last_node_id, "node-y");
}

TEST(UdafDiscoveryBridge, OnHeartbeatForwards) {
    BridgeTestCB cb;
    DiscoveryBridge bridge(&cb);
    bridge.on_node_heartbeat("node-z");
    EXPECT_EQ(cb.hbs.load(), 1);
    EXPECT_EQ(cb.last_node_id, "node-z");
}

TEST(UdafDiscoveryBridge, NoCallbacksReturnsErr) {
    DiscoveryBridge bridge(nullptr);
    EXPECT_FALSE(bridge.has_callbacks());
    std::vector<std::string> svcs;
    auto r1 = bridge.on_node_join("n", "h", "127.0.0.1", 1, svcs);
    auto r2 = bridge.on_node_leave("n");
    EXPECT_TRUE(r1.is_err());
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r1.error(), udaf::core::ErrorCode::INVALID_ARG);
    // heartbeat 在无 callback 时静默
    bridge.on_node_heartbeat("n");  // 不崩
}

// ---------- B2: UdpSocket ----------

TEST(UdafTransport, UdpBindAndSend) {
    auto s1 = UdpSocket::create(0);  // 自动分配端口
    ASSERT_TRUE(s1.is_ok());
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s2.is_ok());

    auto p1 = s1.value()->bound_port();
    auto p2 = s2.value()->bound_port();
    EXPECT_NE(p1, 0);
    EXPECT_NE(p2, 0);

    std::vector<std::uint8_t> msg{0x01, 0x02, 0x03, 0x04};
    auto r = s1.value()->send(msg, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok()) << "send failed: " << static_cast<int>(r.error());

    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    EXPECT_EQ(recv.value(), msg);
}

TEST(UdafTransport, UnicastRateLimit) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    auto dst = Endpoint{"127.0.0.1", 19999, false};
    std::vector<std::uint8_t> msg{1, 2, 3};

    // 5/s 限流：第 6 次应返回 NET_RATE_LIMITED
    int ok = 0, limited = 0;
    for (int i = 0; i < 10; ++i) {
        auto r = s.value()->send(msg, dst);
        if (r.is_ok()) ++ok;
        else if (r.error() == ErrorCode::NET_RATE_LIMITED) ++limited;
    }
    EXPECT_EQ(ok, 5);
    EXPECT_EQ(limited, 5);
}

TEST(UdafTransport, BroadcastRateLimit) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    auto eb = s.value()->enable_broadcast();
    ASSERT_TRUE(eb.is_ok());
    Endpoint dst{"127.255.255.255", 19998, true};
    std::vector<std::uint8_t> msg{1};

    auto r1 = s.value()->send(msg, dst);
    auto r2 = s.value()->send(msg, dst);  // 30s 内第二次
    EXPECT_TRUE(r1.is_ok() || r1.error() == ErrorCode::NET_BROADCAST_FAILED);
    EXPECT_EQ(r2.error(), ErrorCode::NET_RATE_LIMITED);
}

TEST(UdafTransport, EncryptedPayload) {
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    std::vector<std::uint8_t> psk(32, 0xAA);
    s1.value()->set_psk(psk);
    s2.value()->set_psk(psk);

    std::vector<std::uint8_t> msg(64);
    for (std::size_t i = 0; i < msg.size(); ++i) {
        msg[i] = static_cast<std::uint8_t>(i);
    }
    auto p2 = s2.value()->bound_port();
    auto r = s1.value()->send(msg, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());
    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    EXPECT_EQ(recv.value(), msg);
}

TEST(UdafTransport, EncryptedDecryptFailure) {
    // 发送方与接收方用不同 PSK → 解密失败
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    std::vector<std::uint8_t> psk1(32, 0xAA);
    std::vector<std::uint8_t> psk2(32, 0xBB);  // 不同的 PSK
    s1.value()->set_psk(psk1);
    s2.value()->set_psk(psk2);

    std::vector<std::uint8_t> msg(64, 0x42);
    auto p2 = s2.value()->bound_port();
    auto r = s1.value()->send(msg, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());
    auto recv = s2.value()->recv(1000);
    // 解密失败 → 返回错误
    EXPECT_TRUE(recv.is_err());
}

TEST(UdafTransport, BoundPortAndClose) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    EXPECT_GT(s.value()->bound_port(), 0);
    s.value()->close();
    // 关闭后 recv 应失败
    auto r = s.value()->recv(50);
    EXPECT_TRUE(r.is_err());
}

TEST(UdafTransport, RecvTimeout) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    auto r = s.value()->recv(50);
    EXPECT_EQ(r.error(), ErrorCode::NET_TIMEOUT);
}

TEST(UdafTransport, BindEaddrInUse) {
    // 覆盖 udp_socket.cpp 行 47-49 端口占用 → RESOURCE_BUSY
    auto s1 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok());
    auto port = s1.value()->bound_port();
    auto s2 = UdpSocket::create(port);  // 同端口再次 bind
    EXPECT_TRUE(s2.is_err());
    EXPECT_EQ(s2.error(), ErrorCode::RESOURCE_BUSY);
}

TEST(UdafTransport, UnicastRateLimited) {
    // 覆盖 udp_socket.cpp 行 130-132 单播频率限制
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    auto r = UdpSocket::create(0);
    ASSERT_TRUE(r.is_ok());
    auto dst_port = r.value()->bound_port();
    Endpoint dst{"127.0.0.1", dst_port, false};
    std::vector<std::uint8_t> payload{1, 2, 3};
    // 前 5 次单播应通过
    for (int i = 0; i < 5; ++i) {
        auto x = s.value()->send(payload, dst);
        EXPECT_TRUE(x.is_ok()) << "iter " << i;
    }
    // 第 6 次应被限流
    auto blocked = s.value()->send(payload, dst);
    EXPECT_EQ(blocked.error(), ErrorCode::NET_RATE_LIMITED);
}

TEST(UdafTransport, BroadcastSuccessAndRateLimited) {
    // 覆盖 udp_socket.cpp 行 110-118 广播频率限制（30s 一次）
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    ASSERT_TRUE(s.value()->enable_broadcast().is_ok());
    Endpoint bcast{"255.255.255.255", 9999, true};
    std::vector<std::uint8_t> payload{0x01};
    auto first = s.value()->send(payload, bcast);
    EXPECT_TRUE(first.is_ok());
    // 30s 内第二次应被限流
    auto second = s.value()->send(payload, bcast);
    EXPECT_EQ(second.error(), ErrorCode::NET_RATE_LIMITED);
}

TEST(UdafTransport, SendInvalidAddress) {
    // 覆盖 udp_socket.cpp 行 153-155 inet_pton 失败 → INVALID_ARG
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    std::vector<std::uint8_t> payload{1, 2, 3};
    auto r = s.value()->send(payload, Endpoint{"not.an.ip.addr", 1234, false});
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST(UdafTransport, SendAfterClose) {
    // 覆盖 udp_socket.cpp 行 122-124 fd 关闭后 send → NET_SOCKET_CLOSED
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    s.value()->close();
    std::vector<std::uint8_t> payload{1};
    auto r = s.value()->send(payload, Endpoint{"127.0.0.1", 1234, false});
    EXPECT_EQ(r.error(), ErrorCode::NET_SOCKET_CLOSED);
}

// ===== 边缘用例：覆盖 registry 全部错误分支 =====

TEST(UdafRegistry, RegisterEmptyNodeIdReturnsErr) {
    ServiceRegistry reg;
    auto r = reg.register_node(make_entry(""));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafRegistry, GetNonExistentReturnsErr) {
    ServiceRegistry reg;
    auto r = reg.get_node("missing");
    ASSERT_TRUE(r.is_err());
}

TEST(UdafRegistry, UnregisterNonExistentReturnsFalse) {
    ServiceRegistry reg;
    auto r = reg.unregister_node("ghost");
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

TEST(UdafRegistry, EmptyRegistrySnapshot) {
    ServiceRegistry reg;
    auto snap = reg.snapshot();
    EXPECT_EQ(snap.size(), 0u);
    EXPECT_EQ(reg.size(), 0u);
}

TEST(UdafRegistry, ClearRemovesAll) {
    ServiceRegistry reg;
    reg.register_node(make_entry("a"));
    reg.register_node(make_entry("b"));
    EXPECT_EQ(reg.size(), 2u);
    reg.clear();
    EXPECT_EQ(reg.size(), 0u);
    EXPECT_TRUE(reg.snapshot().empty());
}

TEST(UdafRegistry, MultipleSubscribers) {
    ServiceRegistry reg;
    std::atomic<int> a{0}, b{0};
    auto ha = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++a; });
    auto hb = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++b; });
    ASSERT_NE(ha, nullptr);
    ASSERT_NE(hb, nullptr);
    reg.register_node(make_entry("x"));
    EXPECT_EQ(a.load(), 1);
    EXPECT_EQ(b.load(), 1);
    // 释放一个不影响另一个
    ha.reset();
    reg.register_node(make_entry("y"));
    EXPECT_EQ(a.load(), 1);
    EXPECT_EQ(b.load(), 2);
}

TEST(UdafRegistry, UnregisterNotifiesSubscribers) {
    ServiceRegistry reg;
    std::atomic<int> rm{0};
    auto h = reg.subscribe([&](RegistryEvent ev, const RegistryEntry&) {
        if (ev == RegistryEvent::Remove) ++rm;
    });
    ASSERT_NE(h, nullptr);
    reg.register_node(make_entry("z"));
    reg.unregister_node("z");
    EXPECT_EQ(rm.load(), 1);
}

TEST(UdafRegistry, SubscribeMovedHandleUniqueId) {
    ServiceRegistry reg;
    auto h1 = reg.subscribe([](RegistryEvent, const RegistryEntry&){});
    auto h2 = reg.subscribe([](RegistryEvent, const RegistryEntry&){});
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);
    EXPECT_NE(h1->id(), h2->id());
    EXPECT_TRUE(h1->valid());
    EXPECT_TRUE(h2->valid());
}

// ---------- Advertisement 序列化 ----------
using udaf::ability_a::discovery::AdvertisementPayload;
using udaf::ability_a::discovery::serialize_payload;
using udaf::ability_a::discovery::parse_payload;

TEST(UdafAdvertisement, RoundTrip) {
    AdvertisementPayload p;
    p.node_id      = "host-001";
    p.hostname     = "ubuntu-host";
    p.bind_address = "192.168.1.10";
    p.bind_port    = 9000;
    p.services     = {"cmd-exec", "file-xfer", "heartbeat"};
    auto buf = serialize_payload(p);
    auto p2 = parse_payload(buf);
    EXPECT_EQ(p2.node_id,      p.node_id);
    EXPECT_EQ(p2.hostname,     p.hostname);
    EXPECT_EQ(p2.bind_address, p.bind_address);
    EXPECT_EQ(p2.bind_port,    p.bind_port);
    EXPECT_EQ(p2.services,     p.services);
}

TEST(UdafAdvertisement, RoundTripEmpty) {
    AdvertisementPayload p;
    p.node_id = "x";
    p.hostname = "y";
    p.bind_address = "z";
    p.bind_port = 0;
    p.services = {};
    auto buf = serialize_payload(p);
    auto p2 = parse_payload(buf);
    EXPECT_EQ(p2.node_id,  "x");
    EXPECT_EQ(p2.services.size(), 0u);
}

TEST(UdafAdvertisement, ParseTruncatedReturnsEmpty) {
    auto p = parse_payload(std::span<const std::uint8_t>{});
    EXPECT_TRUE(p.node_id.empty());
    EXPECT_TRUE(p.services.empty());

    // 只有 1 字节
    std::uint8_t one = 0;
    auto p2 = parse_payload(std::span<const std::uint8_t>(&one, 1));
    EXPECT_TRUE(p2.node_id.empty());
}

TEST(UdafAdvertisement, ParseTooShort) {
    // node_id 长度字段存在但截断
    std::vector<std::uint8_t> buf = {0x00, 0x10, 0x41};  // len=16 但只有 1B
    auto p = parse_payload(buf);
    EXPECT_TRUE(p.node_id.empty());
}

// ---------- PeerWhitelist 完整覆盖 ----------
using udaf::ability_a::trust::PeerWhitelist;
using udaf::ability_a::trust::WhitelistEntry;

namespace {

WhitelistEntry make_wl(std::string id, std::vector<std::uint8_t> fp = {},
                       std::unordered_set<std::string> caps = {}) {
    WhitelistEntry e;
    e.node_id = std::move(id);
    e.fingerprint_sha256_ = std::move(fp);
    e.allowed_capabilities_ = std::move(caps);
    return e;
}

}  // namespace

TEST(UdafTrust, AddAndContains) {
    PeerWhitelist w;
    std::vector<std::uint8_t> fp(32, 0xAA);
    auto r = w.add(make_wl("d1", fp, {"cmd", "file"}));
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());  // 新增

    EXPECT_TRUE(w.contains("d1"));
    EXPECT_TRUE(w.contains("d1", "cmd"));
    EXPECT_FALSE(w.contains("d1", "net"));
    EXPECT_FALSE(w.contains("ghost"));
    EXPECT_EQ(w.size(), 1u);
}

TEST(UdafTrust, AddRejectsEmptyNodeId) {
    PeerWhitelist w;
    auto r = w.add(make_wl("", std::vector<std::uint8_t>(32, 0xAA)));
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}

TEST(UdafTrust, AddRejectsBadFingerprint) {
    PeerWhitelist w;
    auto r1 = w.add(make_wl("d1", {}));  // 空 fingerprint
    EXPECT_TRUE(r1.is_err());
    auto r2 = w.add(make_wl("d2", std::vector<std::uint8_t>(16, 0xAA)));  // 16B
    EXPECT_TRUE(r2.is_err());
}

TEST(UdafTrust, AddExistingReturnsFalse) {
    PeerWhitelist w;
    std::vector<std::uint8_t> fp(32, 0xAA);
    EXPECT_TRUE(w.add(make_wl("d1", fp)).value());
    EXPECT_FALSE(w.add(make_wl("d1", fp)).value());  // 重复 → is_new=false
}

TEST(UdafTrust, Remove) {
    PeerWhitelist w;
    std::vector<std::uint8_t> fp(32, 0xAA);
    w.add(make_wl("d1", fp));
    auto r = w.remove("d1");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());
    EXPECT_FALSE(w.contains("d1"));

    // 再次 remove
    EXPECT_FALSE(w.remove("d1").value());
}

TEST(UdafTrust, GetAndMissing) {
    PeerWhitelist w;
    std::vector<std::uint8_t> fp(32, 0xBB);
    w.add(make_wl("d1", fp, {"cmd"}));
    auto r = w.get("d1");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().node_id, "d1");
    EXPECT_EQ(r.value().fingerprint_sha256_.size(), 32u);

    auto r2 = w.get("ghost");
    EXPECT_TRUE(r2.is_err());
}

TEST(UdafTrust, ClearAll) {
    PeerWhitelist w;
    w.add(make_wl("d1", std::vector<std::uint8_t>(32, 0xAA)));
    w.add(make_wl("d2", std::vector<std::uint8_t>(32, 0xBB)));
    EXPECT_EQ(w.size(), 2u);
    w.clear();
    EXPECT_EQ(w.size(), 0u);
    EXPECT_FALSE(w.contains("d1"));
}