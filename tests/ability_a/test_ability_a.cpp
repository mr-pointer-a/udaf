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
#include "ability_a/discovery/advertiser.hpp"
#include "ability_a/discovery/scanner.hpp"
#include "ability_a/trust/peer_whitelist.hpp"
#include "ability_a/bridge/discovery_bridge.hpp"
#include "bridge/topology_update_callbacks.hpp"
#include "crypto/hmac.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>
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

// 覆盖 service_registry.cpp 行 11-25 SubscriptionHandle 移动构造/赋值
// （之前的测试移动的是 unique_ptr<Handle>，不是 Handle 本身）
TEST(UdafRegistry, SubscriptionHandleDirectMove) {
    using udaf::ability_a::registry::SubscriptionHandle;
    ServiceRegistry reg;
    std::atomic<int> cnt{0};
    // 移动构造：从 unique_ptr 持有对象移动构造出独立 Handle
    auto h_unique = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++cnt; });
    ASSERT_NE(h_unique, nullptr);
    auto id = h_unique->id();
    SubscriptionHandle moved(std::move(*h_unique));
    h_unique.reset();
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.id(), id);

    // 移动赋值：再订阅一个，把 Handle 移动赋值过去
    auto h2_unique = reg.subscribe([&](RegistryEvent, const RegistryEntry&) { ++cnt; });
    ASSERT_NE(h2_unique, nullptr);
    auto id2 = h2_unique->id();
    moved = std::move(*h2_unique);
    h2_unique.reset();
    EXPECT_EQ(moved.id(), id2);
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

// 覆盖 udp_socket.cpp:103 限流历史条目过期后清理
TEST(UdafTransport, UnicastRateLimitEvictsExpiredEntries) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    auto dst = Endpoint{"127.0.0.1", 19997, false};
    std::vector<std::uint8_t> msg{1};

    // 5/s 限流
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(s.value()->send(msg, dst).is_ok());
    }
    // 第 6 次立即应被限流
    EXPECT_EQ(s.value()->send(msg, dst).error(), ErrorCode::NET_RATE_LIMITED);

    // 等待 1.1s 让历史条目过期（覆盖 hist.erase 分支）
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // 现在应能再次发送
    auto r = s.value()->send(msg, dst);
    EXPECT_TRUE(r.is_ok()) << "1s 后限流历史应清空，error=" << static_cast<int>(r.error());
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

// ===== UDP 覆盖率补充 =====

TEST(UdafTransport, CreateExplicitPortSuccess) {
    auto s = UdpSocket::create(19999);  // 显式端口
    EXPECT_TRUE(s.is_ok());
}

TEST(UdafTransport, CreatePortZeroGetsEphemeral) {
    auto s = UdpSocket::create(0);  // 系统分配端口
    EXPECT_TRUE(s.is_ok());
}

TEST(UdafTransport, RecvAfterCloseReturnsError) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    s.value()->close();
    auto r = s.value()->recv(100);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::NET_SOCKET_CLOSED);
}

TEST(UdafTransport, EndpointStructBasics) {
    Endpoint ep{"127.0.0.1", 8080, true};
    EXPECT_EQ(ep.address, "127.0.0.1");
    EXPECT_EQ(ep.port, 8080);
    EXPECT_TRUE(ep.is_broadcast);
}

// ===== Round 6 UDP 覆盖率补充 =====

// 覆盖 recv 的 PSK 跳过分支（udp_socket.cpp:201 buf.size() <= 12）
TEST(UdafTransport, RecvWithPskButShortPacketReturnsRaw) {
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    // 设置 PSK 但发送短 payload（payload <= 12 时 buf.size() <= 12，跳过解密）
    std::vector<std::uint8_t> psk(32, 0xCC);
    s1.value()->set_psk(psk);
    s2.value()->set_psk(psk);

    // 发送空 payload：实际 to_send 是空向量，sendto 发送 0 字节
    auto p2 = s2.value()->bound_port();
    std::vector<std::uint8_t> empty_payload;
    auto r = s1.value()->send(empty_payload, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());

    // 接收：buf.size()=0，不满足 >12，应返回原始 buf
    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    EXPECT_TRUE(recv.value().empty());
}

// 覆盖 send 中 PSK 大小不为 32 跳过加密的分支（udp_socket.cpp:137-148）
TEST(UdafTransport, SendWithInvalidPskSizeSkipsEncryption) {
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    // 设置错误大小的 PSK（16 字节而非 32）
    std::vector<std::uint8_t> bad_psk(16, 0xDD);
    s1.value()->set_psk(bad_psk);

    std::vector<std::uint8_t> msg{0x01, 0x02, 0x03, 0x04};
    auto p2 = s2.value()->bound_port();
    auto r = s1.value()->send(msg, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());

    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    // 不加密，原样返回
    EXPECT_EQ(recv.value(), msg);
}

// 覆盖 send 中 PSK 设置但 payload 为空的分支（udp_socket.cpp:137 !payload.empty()）
TEST(UdafTransport, SendWithPskButEmptyPayloadSkipsEncryption) {
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    std::vector<std::uint8_t> psk(32, 0xEE);
    s1.value()->set_psk(psk);

    // 空 payload + PSK：跳过加密路径（!payload.empty() 为假）
    auto p2 = s2.value()->bound_port();
    std::vector<std::uint8_t> empty;
    auto r = s1.value()->send(empty, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());

    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    EXPECT_TRUE(recv.value().empty());
}

// 覆盖 recv 在 PSK 模式下但 buf.size() == 12（恰好 nonce 大小）的边界
TEST(UdafTransport, RecvWithPskExactlyNonceSizeSkipsDecrypt) {
    auto s1 = UdpSocket::create(0);
    auto s2 = UdpSocket::create(0);
    ASSERT_TRUE(s1.is_ok() && s2.is_ok());

    // s1 不设 PSK（明文发送 12 字节），s2 设 PSK
    // s2 接收时 buf.size()==12，不满足 >12，跳过解密分支
    std::vector<std::uint8_t> psk(32, 0xFF);
    s2.value()->set_psk(psk);

    std::vector<std::uint8_t> msg(12, 0xAB);
    auto p2 = s2.value()->bound_port();
    auto r = s1.value()->send(msg, Endpoint{"127.0.0.1", p2, false});
    ASSERT_TRUE(r.is_ok());

    auto recv = s2.value()->recv(1000);
    ASSERT_TRUE(recv.is_ok());
    EXPECT_EQ(recv.value(), msg);  // 原样返回
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

// ===== ServiceRegistry 覆盖率补充 =====

TEST(UdafRegistry, GetMissingNodeReturnsError) {
    ServiceRegistry reg;
    auto r = reg.get_node("ghost-node");
    EXPECT_TRUE(r.is_err());
}

TEST(UdafRegistry, UnregisterMissingReturnsFalse) {
    ServiceRegistry reg;
    auto r = reg.unregister_node("ghost-node");
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());  // 不存在 → false
}

TEST(UdafRegistry, ClearEmptiesRegistry) {
    ServiceRegistry reg;
    RegistryEntry e;
    e.node_id_ = "n1";
    e.bind_address_ = "127.0.0.1";
    e.bind_port_ = 8000;
    (void)reg.register_node(e);
    EXPECT_GT(reg.size(), 0u);
    reg.clear();
    EXPECT_EQ(reg.size(), 0u);
}

TEST(UdafRegistry, SnapshotMultipleEntries) {
    ServiceRegistry reg;
    for (int i = 0; i < 5; ++i) {
        RegistryEntry e;
        e.node_id_      = "node-" + std::to_string(i);
        e.hostname_     = "host-" + std::to_string(i);
        e.bind_address_ = "127.0.0.1";
        e.bind_port_    = static_cast<std::uint16_t>(8000 + i);
        (void)reg.register_node(e);
    }
    auto snap = reg.snapshot();
    EXPECT_EQ(snap.size(), 5u);
    for (const auto& e : snap) {
        EXPECT_FALSE(e.node_id_.empty());
        EXPECT_GT(e.bind_port_, 0u);
    }
}

TEST(UdafRegistry, UnsubscribeInvalidHandleNoCrash) {
    ServiceRegistry reg;
    reg.unsubscribe(99999);  // 不存在的 handle → 应安全忽略
    SUCCEED();
}

// ===== F1: Advertiser / Scanner 直接测试 =====
//
// 注：advertiser/scanner 的真实 UDP 收发需要本机 UDP socket 与广播权限，
// 且 broadcast_once/bind_port 互相影响难以并发。本组测试覆盖 handle_frame
// 解析路径（包含 magic、版本、nonce 重放、payload 解析）以及 Advertiser
// /Scanner 的生命周期与构造分支（sock 创建失败、payload 序列化失败等）。

using udaf::ability_a::discovery::Advertiser;
using udaf::ability_a::discovery::AdvertiserConfig;
using udaf::ability_a::discovery::AdvertisementPayload;
using udaf::ability_a::discovery::Scanner;
using udaf::ability_a::discovery::ScannerConfig;
using udaf::ability_a::discovery::kDiscoveryHeaderSize;

// 构造一个合法的 discovery 帧（48B header + payload），供 Scanner::handle_frame 直接消费
namespace {

std::vector<std::uint8_t> make_valid_frame(const std::string& node_id,
                                            std::array<std::uint8_t, 8> nonce = {}) {
    AdvertisementPayload p;
    p.node_id      = node_id;
    p.hostname     = "host-" + node_id;
    p.bind_address = "127.0.0.1";
    p.bind_port    = 9100;
    p.services     = {"cmd-exec"};
    auto payload = udaf::ability_a::discovery::serialize_payload(p);

    constexpr std::size_t kHeaderSize = 48;
    std::vector<std::uint8_t> frame(kHeaderSize + payload.size(), 0);
    // magic "DCAD"
    frame[0] = 0x44; frame[1] = 0x43; frame[2] = 0x41; frame[3] = 0x44;
    // version = 1
    frame[4] = 0; frame[5] = 0; frame[6] = 0; frame[7] = 1;
    // nonce（8B）
    std::memcpy(frame.data() + 8, nonce.data(), 8);
    // mac（32B）= HMAC-SHA256(key=全0(无PSK), msg=nonce || payload)
    std::vector<std::uint8_t> mac_data;
    mac_data.reserve(8 + payload.size());
    mac_data.insert(mac_data.end(), frame.begin() + 8, frame.begin() + 16);
    mac_data.insert(mac_data.end(), payload.begin(), payload.end());
    std::vector<std::uint8_t> mac_key(32, 0);
    auto mac = udaf::crypto::hmac_sha256(mac_key, mac_data);
    if (mac.is_ok() && mac.value().size() == 32) {
        std::memcpy(frame.data() + 16, mac.value().data(), 32);
    }
    std::memcpy(frame.data() + kHeaderSize, payload.data(), payload.size());
    return frame;
}

// 构造一个 magic 不匹配的非法帧
std::vector<std::uint8_t> make_bad_magic_frame() {
    std::vector<std::uint8_t> frame(64, 0);
    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF; frame[3] = 0xFF;
    return frame;
}

}  // namespace

// Scanner::handle_frame 收到合法帧 → 注册到 registry 并返回 Ok(true)
TEST(UdafScanner, HandleFrameRegistersNode) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);

    auto frame = make_valid_frame("scanner-node-1");
    auto r = scanner->handle_frame(frame);
    ASSERT_TRUE(r.is_ok()) << "valid frame should parse";
    EXPECT_TRUE(r.value());

    auto q = reg.get_node("scanner-node-1");
    ASSERT_TRUE(q.is_ok());
    EXPECT_EQ(q.value().bind_port_, 9100);
    EXPECT_EQ(q.value().services_.size(), 1u);
}

// Scanner::handle_frame 重复 nonce → 第二次返回 Ok(false)（静默去重）
TEST(UdafScanner, ReplayProtectionDuplicatesSilenced) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    scanner->clear_nonces();

    // 同一 frame 内部 nonce 相同（构造时全 0）→ 第二次被重放拦截
    auto frame = make_valid_frame("replay-victim");
    auto r1 = scanner->handle_frame(frame);
    ASSERT_TRUE(r1.is_ok());
    EXPECT_TRUE(r1.value());

    auto r2 = scanner->handle_frame(frame);
    ASSERT_TRUE(r2.is_ok());
    EXPECT_FALSE(r2.value()) << "duplicate nonce should be silently dropped";
}

// Scanner::handle_frame magic 不匹配 → 返回 Err(SERIALIZE_VERSION_MISMATCH)
TEST(UdafScanner, BadMagicReturnsVersionMismatch) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);

    auto frame = make_bad_magic_frame();
    auto r = scanner->handle_frame(frame);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// Scanner::handle_frame 帧长度不足 header → 返回 Err(SERIALIZE_DECODE_FAILED)
TEST(UdafScanner, TruncatedFrameReturnsDecodeFailed) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);

    std::vector<std::uint8_t> short_frame(10, 0);  // < 48B header
    auto r = scanner->handle_frame(short_frame);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::SERIALIZE_DECODE_FAILED);
}

// Scanner 生命周期：start → running → stop
TEST(UdafScanner, Lifecycle) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    EXPECT_FALSE(scanner->running());

    EXPECT_TRUE(scanner->start().is_ok());
    EXPECT_TRUE(scanner->running());
    EXPECT_TRUE(scanner->start().is_err());  // 重复 start → BUSY
    scanner->stop();
    EXPECT_FALSE(scanner->running());
}

// Scanner::create 传入 nullptr registry 仍可创建（运行时 try_send 走 nullptr 安全）
TEST(UdafScanner, CreateWithoutRegistrySucceeds) {
    auto scanner = Scanner::create(nullptr, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    auto frame = make_valid_frame("noreg-node");
    auto r = scanner->handle_frame(frame);
    // 没 registry 时解析仍然成功，但不会写入
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value());
}

// Scanner::seen_count 在收到 frame 后递增
TEST(UdafScanner, SeenCountIncrements) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    scanner->clear_nonces();
    EXPECT_EQ(scanner->seen_count(), 0u);

    auto frame = make_valid_frame("count-node");
    (void)scanner->handle_frame(frame);
    EXPECT_GE(scanner->seen_count(), 1u);
}

// Advertiser 构造与生命周期（不实际广播 UDP，避免本机权限/端口冲突）
TEST(UdafAdvertiser, Lifecycle) {
    AdvertiserConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;          // 自动端口
    cfg.broadcast_port = 19999;
    cfg.period = std::chrono::seconds(60);  // 长周期，避免后台线程实际广播

    AdvertisementPayload payload;
    payload.node_id = "adv-node-1";
    payload.hostname = "adv-host";
    payload.bind_address = "127.0.0.1";
    payload.bind_port = 9000;
    payload.services = {"cmd-exec"};

    auto adv = Advertiser::create(std::move(payload), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_FALSE(adv->running());

    EXPECT_TRUE(adv->start().is_ok());
    EXPECT_TRUE(adv->running());
    EXPECT_TRUE(adv->start().is_err());  // 重复 start → BUSY
    adv->stop();
    EXPECT_FALSE(adv->running());
}

// Advertiser::create 在合法 config 下能成功构造
TEST(UdafAdvertiser, CreateSucceeds) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    AdvertisementPayload p;
    p.node_id = "adv-create-test";
    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
}

// Advertiser 在 PSK 模式下也能构造（不影响后续行为）
TEST(UdafAdvertiser, CreateWithPskSucceeds) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    AdvertisementPayload p;
    p.node_id = "adv-psk-test";
    std::vector<std::uint8_t> psk(32, 0xA5);
    auto adv = Advertiser::create(std::move(p), cfg, psk);
    ASSERT_NE(adv, nullptr);
}

// ============================================================
// F7 新增覆盖：broadcast_once / run() / 线程生命周期 / 错误路径
// ============================================================

// 手动触发一次广播：应返回 OK 且 size >= kDiscoveryHeaderSize + payload 长度
TEST(UdafAdvertiser, BroadcastOnceReturnsOk) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29901;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-broadcast-once";
    p.hostname = "adv-host-1";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9001;
    p.services = {"cmd-exec", "file-xfer"};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    auto r = adv->broadcast_once();
    EXPECT_TRUE(r.is_ok());
    EXPECT_GT(r.value(), kDiscoveryHeaderSize);
}

TEST(UdafAdvertiser, BroadcastOnceWithPskReturnsOk) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29902;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-broadcast-psk";
    p.hostname = "adv-host-2";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9002;
    p.services = {"net-info"};

    std::vector<std::uint8_t> psk(32, 0xB6);
    auto adv = Advertiser::create(std::move(p), cfg, psk);
    ASSERT_NE(adv, nullptr);
    auto r = adv->broadcast_once();
    EXPECT_TRUE(r.is_ok());
    // 加密后帧长 >= header + plaintext 长度 + 12B nonce
    EXPECT_GT(r.value(), kDiscoveryHeaderSize + 12u);
}

// 30 秒内的第二次广播应被 UDP 层速率限制拦截
TEST(UdafAdvertiser, BroadcastOnceRateLimited) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29903;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-rate-limit";
    p.hostname = "adv-host-3";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9003;
    p.services = {"x"};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_TRUE(adv->broadcast_once().is_ok());
    // 第二次：仍在 30s 节流窗口内 → NET_RATE_LIMITED
    auto r2 = adv->broadcast_once();
    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), udaf::core::ErrorCode::NET_RATE_LIMITED);
}

// start() + stop() 后再 broadcast_once()：sock_ 被 close → NET_SOCKET_CLOSED
// 必须先 start（让 running_=true）再 stop 才能真正关闭 sock_。
// 直接 stop() 不 start() 会因 running_.exchange(false) 返回 false 提前 return。
TEST(UdafAdvertiser, BroadcastOnceAfterStopReturnsSocketClosed) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29904;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-after-stop";
    p.hostname = "adv-host-4";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9004;
    p.services = {"y"};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_TRUE(adv->start().is_ok());
    adv->stop();  // 此时 running_=true → 实际关闭 sock_
    auto r = adv->broadcast_once();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::NET_SOCKET_CLOSED);
}

// 启动 + 短周期后台线程 + 停止：线程应正确响应 running_=false 并退出
// 不验证广播次数（UDP 节流会拦住大部分调用），只验证线程生命周期不卡死
TEST(UdafAdvertiser, RunLoopExitsCleanlyOnStop) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29905;
    cfg.period = std::chrono::seconds(0);  // period=0 → run loop 立刻重试，可验证线程响应 stop

    AdvertisementPayload p;
    p.node_id = "adv-run-loop";
    p.hostname = "adv-host-5";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9005;
    p.services = {"z"};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_TRUE(adv->start().is_ok());
    EXPECT_TRUE(adv->running());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    // stop 必须在合理时间内返回（< 2s）
    auto t0 = std::chrono::steady_clock::now();
    adv->stop();
    auto us = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(us, 2000) << "stop took " << us << "ms";
    EXPECT_FALSE(adv->running());
}

// 重复 start() 第二次返回 RESOURCE_BUSY
TEST(UdafAdvertiser, StartTwiceReturnsBusy) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29906;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-start-twice";
    p.hostname = "adv-host-6";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9006;
    p.services = {};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_TRUE(adv->start().is_ok());
    auto r = adv->start();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::RESOURCE_BUSY);
    adv->stop();
}

// 未 start 直接 stop：不崩溃、不抛异常
TEST(UdafAdvertiser, StopWithoutStartIsSafe) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29907;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-stop-without-start";
    p.hostname = "adv-host-7";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9007;
    p.services = {};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    EXPECT_NO_THROW(adv->stop());
    EXPECT_FALSE(adv->running());
}

// 重复 stop() 不崩溃（幂等）
TEST(UdafAdvertiser, DoubleStopIsSafe) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29908;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-double-stop";
    p.hostname = "adv-host-8";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9008;
    p.services = {};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    adv->stop();
    EXPECT_NO_THROW(adv->stop());
    EXPECT_FALSE(adv->running());
}

// 三次 start/stop 循环：start 与 stop 都应正确响应 running_ 状态翻转
TEST(UdafAdvertiser, StartStopCycleRepeated) {
    AdvertiserConfig cfg;
    cfg.bind_port = 0;
    cfg.broadcast_port = 29909;
    cfg.period = std::chrono::seconds(60);

    AdvertisementPayload p;
    p.node_id = "adv-cycle";
    p.hostname = "adv-host-9";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9009;
    p.services = {};

    auto adv = Advertiser::create(std::move(p), cfg);
    ASSERT_NE(adv, nullptr);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(adv->start().is_ok()) << "iter " << i;
        EXPECT_TRUE(adv->running()) << "iter " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        adv->stop();
        EXPECT_FALSE(adv->running()) << "iter " << i;
    }
}

// 端口冲突：先开一个 UDP socket 占住端口，再创建 Advertiser 使用同端口 → nullptr
// 覆盖 advertiser.cpp:26 (UdpSocket::create 失败 → return nullptr)
TEST(UdafAdvertiser, CreateReturnsNullWhenBindPortBusy) {
    // 先占一个端口
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);  // 自动分配
    ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    sockaddr_in actual{};
    socklen_t len = sizeof(actual);
    ASSERT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &len), 0);
    std::uint16_t busy_port = ntohs(actual.sin_port);

    // 用同端口创建 Advertiser → UdpSocket::create 失败 → create 返回 nullptr
    AdvertiserConfig cfg;
    cfg.bind_port = busy_port;
    cfg.broadcast_port = 29910;
    cfg.period = std::chrono::seconds(60);
    AdvertisementPayload p;
    p.node_id = "adv-port-busy";
    p.hostname = "adv-host-10";
    p.bind_address = "127.0.0.1";
    p.bind_port = 9010;
    p.services = {};
    auto adv = Advertiser::create(std::move(p), cfg);
    EXPECT_EQ(adv, nullptr);

    ::close(fd);
}

// ============================================================
// F8 新增覆盖：scanner.cpp 未覆盖分支
// ============================================================

// 构造 PSK 模式的 Scanner（覆盖 scanner.cpp:22 psk_.assign）
TEST(UdafScanner, CreateWithPskSucceeds) {
    ServiceRegistry reg;
    std::vector<std::uint8_t> psk(32, 0xC3);
    auto scanner = Scanner::create(&reg, ScannerConfig{}, psk);
    ASSERT_NE(scanner, nullptr);
}

// version != kDiscoveryVersion → SERIALIZE_VERSION_MISMATCH
// 覆盖 scanner.cpp:82-84
TEST(UdafScanner, BadVersionFrameReturnsVersionMismatch) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    auto frame = make_valid_frame("badver-node");
    // 篡改 version 字段（offset 4..7）为 0x9999_9999
    frame[4] = 0x99; frame[5] = 0x99; frame[6] = 0x99; frame[7] = 0x99;
    auto r = scanner->handle_frame(frame);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// MAC 校验失败 → 静默丢弃（返回 Ok(false)）
// 覆盖 scanner.cpp:124-127
TEST(UdafScanner, BadMacReturnsOkFalseSilently) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    auto frame = make_valid_frame("badmac-node");
    // 篡改 MAC 字段（offset 16..48）
    for (std::size_t i = 16; i < 48; ++i) frame[i] ^= 0xFF;
    auto r = scanner->handle_frame(frame);
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value());
}

// 空 node_id payload → SERIALIZE_DECODE_FAILED
// 覆盖 scanner.cpp:131-133
TEST(UdafScanner, EmptyNodeIdReturnsDecodeFailed) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);

    // 构造 payload 为空 node_id 的合法帧
    AdvertisementPayload p;
    p.node_id      = "";  // 故意空
    p.hostname     = "ghost";
    p.bind_address = "127.0.0.1";
    p.bind_port    = 9200;
    p.services     = {"x"};
    auto payload = udaf::ability_a::discovery::serialize_payload(p);
    constexpr std::size_t kHeaderSize = 48;
    std::vector<std::uint8_t> frame(kHeaderSize + payload.size(), 0);
    frame[0] = 0x44; frame[1] = 0x43; frame[2] = 0x41; frame[3] = 0x44;
    frame[4] = 0; frame[5] = 0; frame[6] = 0; frame[7] = 1;
    std::vector<std::uint8_t> mac_data;
    mac_data.insert(mac_data.end(), frame.begin() + 8, frame.begin() + 16);
    mac_data.insert(mac_data.end(), payload.begin(), payload.end());
    std::vector<std::uint8_t> mac_key(32, 0);
    auto mac = udaf::crypto::hmac_sha256(mac_key, mac_data);
    if (mac.is_ok()) std::memcpy(frame.data() + 16, mac.value().data(), 32);
    std::memcpy(frame.data() + kHeaderSize, payload.data(), payload.size());

    auto r = scanner->handle_frame(frame);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::SERIALIZE_DECODE_FAILED);
}

// replay window 过期清理：构造两个不同 nonce 的帧，先记录 nonce_A，
// 用 replay_window=0s（立即过期），再 send frame_B → 应清理 nonce_A 后注册 frame_B
// 覆盖 scanner.cpp:101-107
TEST(UdafScanner, ReplayWindowCleanupRemovesExpired) {
    ServiceRegistry reg;
    ScannerConfig cfg;
    cfg.replay_window = std::chrono::seconds(0);  // 立即过期
    auto scanner = Scanner::create(&reg, cfg);
    ASSERT_NE(scanner, nullptr);

    // 先注册 nonce_A
    std::array<std::uint8_t, 8> nonce_a{};
    nonce_a[0] = 0xAA;
    auto frame_a = make_valid_frame("replay-a", nonce_a);
    auto r_a = scanner->handle_frame(frame_a);
    EXPECT_TRUE(r_a.is_ok());
    EXPECT_TRUE(r_a.value());
    EXPECT_EQ(scanner->seen_count(), 1u);

    // 短睡眠确保时间前进（steady_clock 至少 1ns）
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 用 nonce_B 重新发送 frame_a（模拟重发但 nonce 不同 → 触发清理旧 nonce）
    std::array<std::uint8_t, 8> nonce_b{};
    nonce_b[0] = 0xBB;
    auto frame_b = make_valid_frame("replay-b", nonce_b);
    auto r_b = scanner->handle_frame(frame_b);
    EXPECT_TRUE(r_b.is_ok());
    EXPECT_TRUE(r_b.value());
    // 此时 seen_nonces_ 应只剩 nonce_B（nonce_A 已被清理）
    EXPECT_EQ(scanner->seen_count(), 1u);
}

// 后台线程启动 + 短时 poll 后停止：验证 thread body（while running.load()）被实际进入，
// poll_once 被调用（recv 超时返回 NET_TIMEOUT，被 catch 并 continue）
// 覆盖 scanner.cpp:36-44, 54-63
TEST(UdafScanner, RunLoopPollsAndStopsCleanly) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    EXPECT_TRUE(scanner->start().is_ok());
    EXPECT_TRUE(scanner->running());
    // 留 350ms 让 thread 跑 3 次 poll_once（每次 recv(100) timeout）
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    auto t0 = std::chrono::steady_clock::now();
    scanner->stop();
    auto us = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(us, 2000) << "stop took " << us << "ms";
    EXPECT_FALSE(scanner->running());
}

// 重复 start 第二次返回 RESOURCE_BUSY
TEST(UdafScanner, StartTwiceReturnsBusy) {
    ServiceRegistry reg;
    auto scanner = Scanner::create(&reg, ScannerConfig{});
    ASSERT_NE(scanner, nullptr);
    EXPECT_TRUE(scanner->start().is_ok());
    auto r = scanner->start();
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::RESOURCE_BUSY);
    scanner->stop();
}

// 真 UDP 套接字回环：向 scanner 绑定的端口发送合法帧，验证 poll_once
// 内部 recv() 成功 → 调用 handle_frame → 注册节点（覆盖 scanner.cpp:62）
TEST(UdafScanner, PollOnceReceivesFrameFromLoopback) {
    ServiceRegistry reg;
    constexpr std::uint16_t kScannerPort = 19902;
    ScannerConfig cfg;
    cfg.bind_port = kScannerPort;
    auto scanner = Scanner::create(&reg, cfg);
    ASSERT_NE(scanner, nullptr);
    ASSERT_TRUE(scanner->start().is_ok());

    // 等后台线程进入 recv()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 构造发送端 UDP 套接字并 sendto 到 scanner 端口
    int tx_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(tx_fd, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(kScannerPort);

    auto frame = make_valid_frame("loopback-node");
    ssize_t sent = ::sendto(tx_fd, frame.data(), frame.size(), 0,
                            reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    EXPECT_EQ(sent, static_cast<ssize_t>(frame.size()));
    ::close(tx_fd);

    // 等待 scanner 接收 + 处理
    bool registered = false;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto entry = reg.get_node("loopback-node");
        if (entry.is_ok()) { registered = true; break; }
    }
    EXPECT_TRUE(registered) << "Scanner never registered loopback-node";

    scanner->stop();
}

// ===== F13 UDP 覆盖率补充 =====

// 绑定特权端口（<1024）作为非 root 用户 → bind 返回 EACCES
// 触发 udp_socket.cpp:50（非 EADDRINUSE 路径）→ 返回 NET_SEND_FAILED
TEST(UdafTransport, BindPrivilegedPortAsNonRootReturnsNetSendFailed) {
    auto s = UdpSocket::create(80);  // 特权端口需要 root
    ASSERT_TRUE(s.is_err());
    // EACCES 走 udp_socket.cpp:50 的 else 分支，区别于 EADDRINUSE → RESOURCE_BUSY
    EXPECT_EQ(s.error(), ErrorCode::NET_SEND_FAILED);
}

// 关闭 fd 后调用 enable_broadcast → setsockopt(-1, ...) 返回 EBADF
// 触发 udp_socket.cpp:88 → 返回 NET_BROADCAST_FAILED
TEST(UdafTransport, EnableBroadcastAfterCloseReturnsError) {
    auto s = UdpSocket::create(0);
    ASSERT_TRUE(s.is_ok());
    s.value()->close();
    auto r = s.value()->enable_broadcast();
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::NET_BROADCAST_FAILED);
}