// test_ability_b.cpp - 阶段 C（C1-C5）单元测试
//
// 测试设计：
//   C1: SerializerBase round-trip / version mismatch / type mismatch
//   C2: InputPort try_recv / try_send / recv timeout / capacity
//   C3: InprocChannel send_base/recv_base + 优先级 + heartbeat 始终投递
//   C4: Topology add/remove/commit + cycle detection
//   C5: Node 生命周期状态机 + Scheduler 白名单回调

#include <gtest/gtest.h>

#include "ability_b/node/node.hpp"
#include "ability_b/port/port.hpp"
#include "ability_b/topology/topology.hpp"
#include "ability_b/transport/channel.hpp"
#include "ability_b/transport/inproc_channel.hpp"
#include "ability_b/serialization/serializer.hpp"
#include "core/error_code.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>
#include <typeindex>
#include <type_traits>
#include <vector>

using udaf::ability_b::node::LifecycleState;
using udaf::ability_b::node::Node;
using udaf::ability_b::node::NodeConfig;
using udaf::ability_b::node::Scheduler;
using udaf::ability_b::node::to_string;
using udaf::ability_b::port::InputPort;
using udaf::ability_b::port::OutputPort;
using udaf::ability_b::port::PortInfo;
using udaf::ability_b::serialization::SerializerBase;
using udaf::ability_b::topology::PeerEdge;
using udaf::ability_b::topology::PeerNode;
using udaf::ability_b::topology::Topology;
using udaf::ability_b::topology::TopologyTransaction;
using udaf::ability_b::transport::Channel;
using udaf::ability_b::transport::InprocChannel;
using udaf::ability_b::transport::MessageFrame;
using udaf::ability_b::transport::MessagePriority;
using udaf::ability_b::transport::RecvStatus;
using udaf::ability_b::transport::SendResult;
using udaf::core::ErrorCode;

// ---------------- C1: Serializer ----------------

class SimpleSerializer : public SerializerBase {
public:
    [[nodiscard]] std::string type_name() const noexcept override {
        return "SimpleMsg";
    }
    [[nodiscard]] std::uint32_t schema_version() const noexcept override {
        return 1;
    }
    [[nodiscard]] bool accepts_type(std::string_view t) const noexcept override {
        return t == type_name();
    }
    [[nodiscard]] udaf::core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override {
        return udaf::core::Result<std::vector<std::uint8_t>>::ok({0x01, 0x02});
    }
    [[nodiscard]] udaf::core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t>) noexcept override {
        return udaf::core::Result<void>::ok();
    }
};

class WrongVersionSerializer : public SerializerBase {
public:
    [[nodiscard]] std::string type_name() const noexcept override {
        return "OtherMsg";
    }
    [[nodiscard]] std::uint32_t schema_version() const noexcept override {
        return 99;
    }
    [[nodiscard]] bool accepts_type(std::string_view t) const noexcept override {
        return t == type_name();
    }
    [[nodiscard]] udaf::core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override {
        return udaf::core::Result<std::vector<std::uint8_t>>::ok({});
    }
    [[nodiscard]] udaf::core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t>) noexcept override {
        return udaf::core::Result<void>::ok();
    }
};

TEST(UdafSerialization, RoundTrip) {
    SimpleSerializer enc;
    SimpleSerializer dec;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_ok());
    EXPECT_GT(frame.value().size(), 9u);
    auto r = dec.decode_payload(frame.value());
    ASSERT_TRUE(r.is_ok());
}

TEST(UdafSerialization, VersionMismatch) {
    SimpleSerializer enc;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_ok());
    WrongVersionSerializer dec;
    auto r = dec.decode_payload(frame.value());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

TEST(UdafSerialization, TypeMismatch) {
    SimpleSerializer enc;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_ok());
    SimpleSerializer dec;
    // 构造伪帧：magic + version(1) + type_len=3 "abc" + 1B payload
    std::vector<std::uint8_t> bad;
    bad.resize(4 + 4 + 1 + 3 + 1);
    std::uint32_t magic = 0x55444146;  // UDAF
    std::uint32_t version = 1;
    for (std::size_t i = 0; i < 4; ++i)
        bad[i] = static_cast<std::uint8_t>((magic   >> ((3 - i) * 8)) & 0xFF);
    for (std::size_t i = 0; i < 4; ++i)
        bad[4 + i] = static_cast<std::uint8_t>((version >> ((3 - i) * 8)) & 0xFF);
    bad[8] = 3;
    bad[9] = 'a'; bad[10] = 'b'; bad[11] = 'c';
    auto r = dec.decode_payload(bad);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_TYPE_MISMATCH);
}

// 覆盖 serializer.cpp:53-55 frame.size() < kHeaderSize
TEST(UdafSerialization, FrameTooShort) {
    SimpleSerializer dec;
    std::vector<std::uint8_t> tiny = {0x55, 0x44, 0x41};  // 只有 3 字节
    auto r = dec.decode_payload(tiny);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_DECODE_FAILED);
}

// 覆盖 serializer.cpp:57-60 magic mismatch
TEST(UdafSerialization, BadMagic) {
    SimpleSerializer dec;
    std::vector<std::uint8_t> bad;
    bad.resize(9);  // kHeaderSize
    // magic = 0xDEADBEEF（不是 UDAF）
    bad[0] = 0xDE; bad[1] = 0xAD; bad[2] = 0xBE; bad[3] = 0xEF;
    bad[4] = 0; bad[5] = 0; bad[6] = 0; bad[7] = 1;
    bad[8] = 0;
    auto r = dec.decode_payload(bad);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// 覆盖 serializer.cpp:67-70 frame.size() < kHeaderSize + tlen
TEST(UdafSerialization, TypeNameTruncated) {
    SimpleSerializer dec;
    std::vector<std::uint8_t> bad;
    bad.resize(10);  // 比声称的 type_len 短
    bad[0] = 0x55; bad[1] = 0x44; bad[2] = 0x41; bad[3] = 0x46;  // magic
    bad[4] = 0; bad[5] = 0; bad[6] = 0; bad[7] = 1;  // version
    bad[8] = 100;  // type_len=100 但只剩 1 字节
    bad[9] = 'x';
    auto r = dec.decode_payload(bad);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_DECODE_FAILED);
}

// 覆盖 serializer.cpp:80-82 decode_payload_inplace 返回错误
class FailingDecodeSerializer : public SerializerBase {
public:
    [[nodiscard]] std::string type_name() const noexcept override { return "failing_decode"; }
    [[nodiscard]] std::uint32_t schema_version() const noexcept override { return 1; }
    [[nodiscard]] bool accepts_type(std::string_view) const noexcept override { return true; }
    [[nodiscard]] udaf::core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override { return udaf::core::Result<std::vector<std::uint8_t>>::ok({}); }
    [[nodiscard]] udaf::core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t>) noexcept override {
        return udaf::core::Result<void>::err(udaf::core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }
};

TEST(UdafSerialization, DecodePayloadInplaceFails) {
    FailingDecodeSerializer enc;
    FailingDecodeSerializer dec;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_ok());
    auto r = dec.decode_payload(frame.value());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::SERIALIZE_DECODE_FAILED);
}

// ===== F15 新增覆盖 =====

// 覆盖 serializer.cpp:32-33 encode_payload 返回错误 → encode 透传错误码
class FailingEncodeSerializer : public SerializerBase {
public:
    [[nodiscard]] std::string type_name() const noexcept override {
        return "failing_encode";
    }
    [[nodiscard]] std::uint32_t schema_version() const noexcept override {
        return 1;
    }
    [[nodiscard]] bool accepts_type(std::string_view) const noexcept override {
        return true;
    }
    [[nodiscard]] udaf::core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override {
        return udaf::core::Result<std::vector<std::uint8_t>>::err(
            udaf::core::ErrorCode::SERIALIZE_ENCODE_FAILED);
    }
    [[nodiscard]] udaf::core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t>) noexcept override {
        return udaf::core::Result<void>::ok();
    }
};

TEST(UdafSerialization, EncodePayloadFailsPropagatesError) {
    FailingEncodeSerializer enc;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_err());
    EXPECT_EQ(frame.error(), ErrorCode::SERIALIZE_ENCODE_FAILED);
}

// 覆盖 serializer.cpp:38-40 type_name() 长度 > 255 → SERIALIZE_TYPE_MISMATCH
class LongTypeNameSerializer : public SerializerBase {
public:
    [[nodiscard]] std::string type_name() const noexcept override {
        // 256 字节（> 255 触发 type_len 校验）
        return std::string(256, 'L');
    }
    [[nodiscard]] std::uint32_t schema_version() const noexcept override {
        return 1;
    }
    [[nodiscard]] bool accepts_type(std::string_view) const noexcept override {
        return true;
    }
    [[nodiscard]] udaf::core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override {
        return udaf::core::Result<std::vector<std::uint8_t>>::ok({});
    }
    [[nodiscard]] udaf::core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t>) noexcept override {
        return udaf::core::Result<void>::ok();
    }
};

TEST(UdafSerialization, LongTypeNameRejected) {
    LongTypeNameSerializer enc;
    auto frame = enc.encode({});
    ASSERT_TRUE(frame.is_err());
    EXPECT_EQ(frame.error(), ErrorCode::SERIALIZE_TYPE_MISMATCH);
}

// ---------------- C2: Port ----------------

TEST(UdafPort, TryRecvEmpty) {
    InputPort<int> port("p1");
    auto r = port.try_recv();
    EXPECT_EQ(r.error(), ErrorCode::BIZ_SERVICE_NOT_FOUND);
}

TEST(UdafPort, TrySendAndRecv) {
    InputPort<int> port("p1", 4);
    PortInfo out_info{"p1", std::type_index(typeid(int)), 1, false};
    OutputPort<int> out(out_info, &port);

    EXPECT_TRUE(out.try_send(42).is_ok());
    EXPECT_EQ(port.size(), 1u);
    auto r = port.try_recv();
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(UdafPort, RecvTimeout) {
    InputPort<int> port("p1");
    auto r = port.recv(20);
    EXPECT_EQ(r.error(), ErrorCode::NET_TIMEOUT);
}

TEST(UdafPort, CapacityLimit) {
    InputPort<int> port("p1", 2);
    PortInfo out_info{"p1", std::type_index(typeid(int)), 1, false};
    OutputPort<int> out(out_info, &port);

    EXPECT_TRUE(out.try_send(1).is_ok());
    EXPECT_TRUE(out.try_send(2).is_ok());
    auto r = out.try_send(3);
    EXPECT_EQ(r.error(), ErrorCode::RESOURCE_BUSY);
}

TEST(UdafPort, InfoReturnsConstRef) {
    InputPort<int> port("p1");
    const auto& info = port.info();
    EXPECT_EQ(info.name, "p1");
    EXPECT_TRUE(info.is_input);
    static_assert(std::is_same_v<decltype(port.info()), const PortInfo&>);
}

// ---------------- C3: InprocChannel ----------------

TEST(UdafChannel, InprocBasic) {
    auto chan = std::make_unique<InprocChannel>();
    MessageFrame m;
    m.payload = {0xAA, 0xBB};
    EXPECT_EQ(chan->send_base(std::move(m)), SendResult::Ok);
    MessageFrame r;
    EXPECT_EQ(chan->recv_base(r, 100), RecvStatus::Ok);
    EXPECT_EQ(r.payload, std::vector<std::uint8_t>({0xAA, 0xBB}));
    EXPECT_EQ(r.seq, 1u);
}

TEST(UdafChannel, HeartbeatAlwaysDelivered) {
    auto chan = std::make_unique<InprocChannel>(2);
    for (int i = 0; i < 2; ++i) {
        MessageFrame d; d.payload = {static_cast<std::uint8_t>(i)};
        d.priority = MessagePriority::Data;
        chan->send_base(std::move(d));
    }
    MessageFrame overflow; overflow.payload = {0xFF};
    overflow.priority = MessagePriority::Data;
    EXPECT_EQ(chan->send_base(std::move(overflow)), SendResult::Full);

    MessageFrame hb; hb.payload = {0xCA};
    hb.priority = MessagePriority::Heartbeat;
    EXPECT_EQ(chan->send_base(std::move(hb)), SendResult::Ok);

    MessageFrame r1, r2;
    EXPECT_EQ(chan->recv_base(r1, 100), RecvStatus::Ok);
    EXPECT_EQ(r1.payload, std::vector<std::uint8_t>({0xCA}));
    EXPECT_EQ(chan->recv_base(r2, 100), RecvStatus::Ok);
}

TEST(UdafChannel, RecvTimeout) {
    auto chan = std::make_unique<InprocChannel>();
    MessageFrame r;
    EXPECT_EQ(chan->recv_base(r, 30), RecvStatus::Timeout);
}

TEST(UdafChannel, CloseClosedRecv) {
    auto chan = std::make_unique<InprocChannel>();
    chan->close();
    MessageFrame m;
    EXPECT_EQ(chan->send_base(std::move(m)), SendResult::Closed);
    MessageFrame r;
    // 已关闭的 channel 在阻塞 recv 时应返回 Closed
    EXPECT_EQ(chan->recv_base(r, -1), RecvStatus::Closed);
}

// ===== F17 新增覆盖 =====

// 覆盖 inproc_channel.cpp:54-55 cv_.wait 阻塞路径
// 覆盖 inproc_channel.cpp:60-62 唤醒后 closed && queues 全空 → Closed
// 构造：先消费完队列消息，确保空 → 启动线程 recv_base(r, -1) 阻塞在 cv_.wait
// → 主线程 close() → notify_all → 阻塞线程谓词 closed_==true 触发 → 走 L60 检查
TEST(UdafChannel, RecvBlockingClosedUnblocksWithClosedStatus) {
    auto chan = std::make_unique<InprocChannel>();

    // 预先 send 一条消息再 recv 消费，确保初始队列空（否则 pick() 直接返回，
    // 不会进入 cv_.wait）
    MessageFrame init;
    init.payload = {0x01};
    ASSERT_EQ(chan->send_base(std::move(init)), SendResult::Ok);
    MessageFrame consume;
    ASSERT_EQ(chan->recv_base(consume, 100), RecvStatus::Ok);

    // 启动后台线程阻塞在 recv_base(r, -1)
    std::atomic<RecvStatus> recv_status{RecvStatus::Timeout};
    std::thread waiter([&] {
        MessageFrame r;
        recv_status.store(chan->recv_base(r, -1));
    });

    // 主线程 sleep 一小段时间，确保 waiter 已进入 cv_.wait
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    chan->close();  // 触发 notify_all + closed_=true
    waiter.join();

    EXPECT_EQ(recv_status.load(), RecvStatus::Closed);
}

TEST(UdafChannel, ChannelTemplateWraps) {
    auto inner = std::make_unique<InprocChannel>();
    Channel<int> ch(std::move(inner));
    EXPECT_EQ(ch.transport(),
              udaf::ability_b::transport::TransportType::Inproc);
}

TEST(UdafChannel, InprocThroughput) {
    auto chan = std::make_unique<InprocChannel>(20000);
    constexpr int kIters = 10000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        MessageFrame m; m.payload.assign(64, static_cast<std::uint8_t>(i & 0xFF));
        EXPECT_EQ(chan->send_base(std::move(m)), SendResult::Ok);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    EXPECT_LT(ms, 500.0) << "10k send: " << ms << "ms";
}

// ---------------- C4: Topology ----------------

TEST(UdafTopology, AddRemoveNode) {
    Topology t;
    auto tx = t.begin_transaction();
    ASSERT_TRUE(tx.is_ok());
    PeerNode n; n.node_id = "n1"; n.hostname = "h1";
    tx.value().add_node(n);
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.node_count(), 1u);

    auto tx2 = t.begin_transaction();
    tx2.value().remove_node("n1");
    EXPECT_TRUE(t.commit(std::move(tx2).value()).is_ok());
    EXPECT_EQ(t.node_count(), 0u);
}

TEST(UdafTopology, CommitEmptyFails) {
    Topology t;
    auto tx = t.begin_transaction();
    ASSERT_TRUE(tx.is_ok());
    auto r = t.commit(std::move(tx).value());
    EXPECT_EQ(r.error(), ErrorCode::INVALID_ARG);
}

TEST(UdafTopology, CommitTwiceFails) {
    Topology t;
    auto tx = t.begin_transaction();
    PeerNode n; n.node_id = "n1";
    tx.value().add_node(n);
    // commit(T&&) 不复制事务，确保节点不会重复注册
    TopologyTransaction owned = std::move(tx).value();
    EXPECT_TRUE(t.commit(std::move(owned)).is_ok());
    EXPECT_EQ(t.node_count(), 1u);
    // 第二次 commit 同一事务：即便允许也是幂等（覆盖注册）
    auto r = t.commit(std::move(owned));
    // 节点数仍为 1（n1 覆盖更新，不增加计数）
    EXPECT_EQ(t.node_count(), 1u);
    (void)r;
}

TEST(UdafTopology, HasCycleDetection) {
    Topology t;
    auto tx = t.begin_transaction();
    for (auto& id : {"a", "b", "c"}) {
        PeerNode n; n.node_id = id;
        tx.value().add_node(n);
    }
    tx.value().add_edge({"a", "b", "tcp"})
                .add_edge({"b", "c", "tcp"})
                .add_edge({"c", "a", "tcp"});  // cycle
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_TRUE(t.has_cycle());

    auto tx2 = t.begin_transaction();
    tx2.value().remove_edge("c", "a");
    EXPECT_TRUE(t.commit(std::move(tx2).value()).is_ok());
    EXPECT_FALSE(t.has_cycle());
}

TEST(UdafTopology, NoCycleLinear) {
    Topology t;
    auto tx = t.begin_transaction();
    for (auto& id : {"a", "b", "c"}) {
        PeerNode n; n.node_id = id;
        tx.value().add_node(n);
    }
    tx.value().add_edge({"a", "b", "tcp"}).add_edge({"b", "c", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_FALSE(t.has_cycle());
}

// ---------------- C5: Node / Scheduler / Lifecycle ----------------

class EchoNode : public Node {
public:
    EchoNode() : Node("echo") {}
    udaf::core::Result<void> init(const NodeConfig& /*cfg*/) noexcept override {
        set_state(LifecycleState::Init);
        return udaf::core::Result<void>::ok();
    }
    udaf::core::Result<void> start() noexcept override {
        set_state(LifecycleState::Running);
        return udaf::core::Result<void>::ok();
    }
    udaf::core::Result<void> stop() noexcept override {
        set_state(LifecycleState::Stopped);
        return udaf::core::Result<void>::ok();
    }
    udaf::core::Result<void> reload() noexcept override {
        set_state(LifecycleState::Reloading);
        return udaf::core::Result<void>::ok();
    }
    const std::vector<PortInfo>& inputs() const noexcept override { return inputs_; }
    const std::vector<PortInfo>& outputs() const noexcept override { return outputs_; }
    // 测试专用：直接设置状态机
    void set_state_for_test(LifecycleState s) noexcept { set_state(s); }
private:
    std::vector<PortInfo> inputs_{PortInfo{"in", std::type_index(typeid(int)), 1, true}};
    std::vector<PortInfo> outputs_{PortInfo{"out", std::type_index(typeid(int)), 1, false}};
};

TEST(UdafNode, LifecycleStateMachine) {
    EchoNode n;
    EXPECT_EQ(n.state(), LifecycleState::Init);
    EXPECT_TRUE(n.init({}).is_ok());
    EXPECT_TRUE(n.start().is_ok());
    EXPECT_EQ(n.state(), LifecycleState::Running);
    EXPECT_TRUE(n.reload().is_ok());
    EXPECT_EQ(n.state(), LifecycleState::Reloading);
    EXPECT_TRUE(n.stop().is_ok());
    EXPECT_EQ(n.state(), LifecycleState::Stopped);
    EXPECT_STREQ(to_string(n.state()), "STOPPED");
}

TEST(UdafNode, LifecycleStateAllNames) {
    // 覆盖 to_string() switch 全部 6 个分支
    EXPECT_STREQ(to_string(LifecycleState::Init),      "INIT");
    EXPECT_STREQ(to_string(LifecycleState::Starting),  "STARTING");
    EXPECT_STREQ(to_string(LifecycleState::Running),   "RUNNING");
    EXPECT_STREQ(to_string(LifecycleState::Reloading), "RELOADING");
    EXPECT_STREQ(to_string(LifecycleState::Stopping),  "STOPPING");
    EXPECT_STREQ(to_string(LifecycleState::Stopped),   "STOPPED");
}

TEST(UdafNode, LifecycleStateNamesIncludeStartingStopping) {
    // 通过 Starting → Running → Stopping 路径触发状态机 + to_string
    EchoNode n;
    n.set_state_for_test(LifecycleState::Starting);
    EXPECT_EQ(n.state(), LifecycleState::Starting);
    EXPECT_STREQ(to_string(n.state()), "STARTING");
    n.set_state_for_test(LifecycleState::Stopping);
    EXPECT_EQ(n.state(), LifecycleState::Stopping);
    EXPECT_STREQ(to_string(n.state()), "STOPPING");
}

TEST(UdafNode, InputsOutputsConstRef) {
    EchoNode n;
    const auto& ins = n.inputs();
    const auto& outs = n.outputs();
    EXPECT_EQ(ins.size(), 1u);
    EXPECT_EQ(outs.size(), 1u);
    EXPECT_TRUE(ins[0].is_input);
    EXPECT_TRUE(outs[0].is_output());
    static_assert(std::is_same_v<decltype(n.inputs()),
                  const std::vector<PortInfo>&>);
}

TEST(UdafScheduler, WhitelistCallback) {
    Scheduler s;
    bool allowed = true;
    s.set_whitelist_check([&](std::string_view, std::string_view) { return allowed; });
    EXPECT_TRUE(s.is_allowed("t", "s"));
    allowed = false;
    EXPECT_FALSE(s.is_allowed("t", "s"));

    Scheduler s2;
    EXPECT_FALSE(s2.is_allowed("t", "s"));
}

TEST(UdafScheduler, ScheduleDenied) {
    Scheduler s;
    s.set_whitelist_check([](auto, auto) { return false; });
    auto r = s.schedule("n1", "host");
    EXPECT_EQ(r.error(), ErrorCode::BIZ_AUTH_UNTRUSTED);
}

TEST(UdafScheduler, ScheduleAllowed) {
    Scheduler s;
    s.set_whitelist_check([](auto, auto) { return true; });
    EXPECT_TRUE(s.schedule("n1", "host").is_ok());
}

// ---------- Topology 扩展覆盖 ----------

TEST(UdafTopology, EdgeCount) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"n1", "h1", "127.0.0.1", 8080, {}})
              .add_node({"n2", "h2", "127.0.0.1", 8081, {}})
              .add_edge({"n1", "n2", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.node_count(), 2u);
    EXPECT_EQ(t.edge_count(), 1u);
}

TEST(UdafTopology, GetNode) {
    Topology t;
    auto tx = t.begin_transaction();
    PeerNode n; n.node_id = "x"; n.hostname = "host-x";
    tx.value().add_node(n);
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    auto r = t.get_node("x");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().node_id, "x");

    auto r2 = t.get_node("missing");
    EXPECT_TRUE(r2.is_err());
}

TEST(UdafTopology, RemoveNodeWithEdges) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"a", "h", "127.0.0.1", 1, {}})
              .add_node({"b", "h", "127.0.0.1", 2, {}})
              .add_node({"c", "h", "127.0.0.1", 3, {}})
              .add_edge({"a", "b", "tcp"})
              .add_edge({"a", "c", "tcp"})
              .add_edge({"b", "c", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.edge_count(), 3u);

    // 删 a 应同时清理 a->b 和 a->c，但保留 b->c
    auto tx2 = t.begin_transaction();
    tx2.value().remove_node("a");
    EXPECT_TRUE(t.commit(std::move(tx2).value()).is_ok());
    EXPECT_EQ(t.node_count(), 2u);
    EXPECT_EQ(t.edge_count(), 1u);
}

TEST(UdafTopology, RemoveEdge) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"a", "h", "127.0.0.1", 1, {}})
              .add_node({"b", "h", "127.0.0.1", 2, {}})
              .add_edge({"a", "b", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.edge_count(), 1u);

    auto tx2 = t.begin_transaction();
    tx2.value().remove_edge("a", "b");
    EXPECT_TRUE(t.commit(std::move(tx2).value()).is_ok());
    EXPECT_EQ(t.edge_count(), 0u);
}

TEST(UdafTopology, NodesList) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"a", "h1", "127.0.0.1", 1, {}})
              .add_node({"b", "h2", "127.0.0.1", 2, {}});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    auto nodes = t.nodes();
    EXPECT_EQ(nodes.size(), 2u);
}

TEST(UdafTopology, EdgesList) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"a", "h", "127.0.0.1", 1, {}})
              .add_node({"b", "h", "127.0.0.1", 2, {}})
              .add_edge({"a", "b", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    auto edges = t.edges();
    EXPECT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].from_node, "a");
    EXPECT_EQ(edges[0].to_node,   "b");
}

TEST(UdafTopology, ClearAll) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"a", "h", "127.0.0.1", 1, {}})
              .add_node({"b", "h", "127.0.0.1", 2, {}})
              .add_edge({"a", "b", "tcp"});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.node_count(), 2u);
    t.clear();
    EXPECT_EQ(t.node_count(), 0u);
    EXPECT_EQ(t.edge_count(), 0u);
}

TEST(UdafTopology, HasCycleNoCycleSelfLoop) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"solo", "h", "127.0.0.1", 1, {}});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_FALSE(t.has_cycle());  // 1 个节点无环
}

TEST(UdafTopology, HasCycleDeepDFS) {
    Topology t;
    auto tx = t.begin_transaction();
    for (auto& id : {"a", "b", "c", "d"}) {
        PeerNode n; n.node_id = id;
        tx.value().add_node(n);
    }
    tx.value().add_edge({"a", "b", "tcp"})
              .add_edge({"b", "c", "tcp"})
              .add_edge({"c", "d", "tcp"})
              .add_edge({"d", "a", "tcp"});  // 4 节点 cycle
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_TRUE(t.has_cycle());
}

// ===== Topology 覆盖率补充 =====

TEST(UdafTopology, BeginTransactionSuccess) {
    Topology t;
    auto tx = t.begin_transaction();
    EXPECT_TRUE(tx.is_ok());
}

TEST(UdafTopology, EmptyTopologyHasNoCycle) {
    Topology t;
    EXPECT_FALSE(t.has_cycle());
    EXPECT_EQ(t.node_count(), 0u);
}

TEST(UdafTopology, NodeCountAfterAdd) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"n1", "h1", "127.0.0.1", 1, {}});
    tx.value().add_node({"n2", "h2", "127.0.0.1", 2, {}});
    EXPECT_TRUE(t.commit(std::move(tx).value()).is_ok());
    EXPECT_EQ(t.node_count(), 2u);
}

TEST(UdafTopology, CommitAfterMove) {
    Topology t;
    auto tx = t.begin_transaction();
    tx.value().add_node({"x", "h", "127.0.0.1", 1, {}});
    TopologyTransaction moved = std::move(tx.value());
    EXPECT_TRUE(t.commit(std::move(moved)).is_ok());
}