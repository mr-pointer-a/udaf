// test_tcp_channel.cpp - TcpChannel 单元测试
//
// 测试策略：
//   1. 每个测试在临时端口启动一个 ZMQ ROUTER 服务端
//   2. TcpChannel (DEALER) 连接该端口
//   3. 验证 send_base / recv_base 帧序列化、错误码、关闭语义
//
// 不依赖能力 C 节点，独立验证 transport 层契约。

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <vector>

#include <zmq.h>

#include "ability_b/transport/channel.hpp"
#include "ability_b/transport/tcp_channel.hpp"
#include "core/error_code.hpp"

namespace {

using udaf::ability_b::transport::ChannelBase;
using udaf::ability_b::transport::MessageFrame;
using udaf::ability_b::transport::MessagePriority;
using udaf::ability_b::transport::RecvStatus;
using udaf::ability_b::transport::SendResult;
using udaf::ability_b::transport::TcpChannel;
using udaf::ability_b::transport::TcpChannelConfig;

// ============================================================================
// ZmqRouterFixture：起一个 ROUTER 监听 TCP 随机端口，返回 "tcp://127.0.0.1:PORT"
// ============================================================================
class ZmqRouterFixture {
public:
    ZmqRouterFixture() {
        ctx_ = zmq_ctx_new();
        sock_ = zmq_socket(ctx_, ZMQ_ROUTER);
        // 随机端口
        int rc = zmq_bind(sock_, "tcp://127.0.0.1:*");
        if (rc != 0) {
            std::fprintf(stderr, "zmq_bind failed: %s\n", zmq_strerror(errno));
            return;
        }
        // 获取实际端口
        char endpoint[256] = {0};
        std::size_t endpoint_len = sizeof(endpoint);
        rc = zmq_getsockopt(sock_, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
        if (rc != 0 || endpoint_len == 0) {
            std::fprintf(stderr, "zmq_getsockopt LAST_ENDPOINT failed\n");
            return;
        }
        // ZMQ_LAST_ENDPOINT 返回的字符串末尾不含 '\0'，按 length 构造
        endpoint_.assign(endpoint, endpoint_len);
        bound_ = true;
    }

    ~ZmqRouterFixture() {
        if (sock_) zmq_close(sock_);
        if (ctx_) {
            int linger = 0;
            zmq_ctx_set(ctx_, ZMQ_LINGER, linger);
            zmq_ctx_term(ctx_);
        }
    }

    [[nodiscard]] bool ready() const noexcept { return bound_; }
    [[nodiscard]] const std::string& endpoint() const noexcept { return endpoint_; }

    /// 接收一帧原始字节（带 routing envelope）
    /// ROUTER 接收来自 DEALER 的格式：[identity][payload]（无 delimiter）
    /// @return 收到的 payload；empty 表示超时或错误
    [[nodiscard]] std::vector<std::uint8_t> recv_raw(int timeout_ms = 1000) {
        std::vector<std::uint8_t> out;
        int rcvto = timeout_ms;
        zmq_setsockopt(sock_, ZMQ_RCVTIMEO, &rcvto, sizeof(rcvto));

        // 第一帧：routing identity（DEALER 首次连接时由 ROUTER 自动生成空 identity）
        zmq_msg_t identity;
        zmq_msg_init(&identity);
        int rc = zmq_msg_recv(&identity, sock_, 0);
        if (rc < 0) {
            zmq_msg_close(&identity);
            return out;
        }
        // 把 identity 缓存起来（用于后续 send_raw 转发）
        // ROUTER 必须以 identity + payload 形式回复
        const std::size_t id_len = zmq_msg_size(&identity);
        last_identity_.resize(id_len);
        if (id_len > 0) {
            std::memcpy(last_identity_.data(), zmq_msg_data(&identity), id_len);
        }
        // 第二帧：payload（DEALER→ROUTER 没有 delimiter 帧）
        zmq_msg_close(&identity);

        zmq_msg_t payload;
        zmq_msg_init(&payload);
        rc = zmq_msg_recv(&payload, sock_, 0);
        if (rc < 0) {
            zmq_msg_close(&payload);
            return out;
        }
        const std::size_t len = zmq_msg_size(&payload);
        out.resize(len);
        if (len > 0) {
            std::memcpy(out.data(), zmq_msg_data(&payload), len);
        }
        zmq_msg_close(&payload);
        return out;
    }

    /// 发送一帧原始字节到上次 recv_raw 收到的客户端
    /// ROUTER→DEALER 格式：[identity][payload]（不含 delimiter，否则 DEALER
    /// 首帧收到 0 字节空帧导致上层解码失败）
    void send_raw(const std::uint8_t* data, std::size_t len) {
        // identity 帧（用于路由）
        zmq_msg_t identity;
        zmq_msg_init_size(&identity, last_identity_.size());
        if (!last_identity_.empty()) {
            std::memcpy(zmq_msg_data(&identity), last_identity_.data(), last_identity_.size());
        }
        zmq_msg_send(&identity, sock_, ZMQ_SNDMORE);
        zmq_msg_close(&identity);

        // payload（不带 SNDMORE，ROUTER 用 identity 路由后，DEALER 直接收到 payload）
        zmq_msg_t payload;
        zmq_msg_init_size(&payload, len);
        if (len > 0) std::memcpy(zmq_msg_data(&payload), data, len);
        zmq_msg_send(&payload, sock_, 0);
        zmq_msg_close(&payload);
    }

private:
    void* ctx_ = nullptr;
    void* sock_ = nullptr;
    bool bound_ = false;
    std::string endpoint_;
    std::vector<std::uint8_t> last_identity_;
};

// ============================================================================
// 解码 TcpChannel 发送的帧：priority(1) + seq(8LE) + plen(4LE) + payload
// ============================================================================
struct DecodedFrame {
    MessagePriority priority;
    std::uint64_t seq;
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] static bool decode_frame_bytes(const std::uint8_t* d, std::size_t n,
                                             DecodedFrame& out) {
    if (n < 13) return false;
    out.priority = static_cast<MessagePriority>(d[0]);
    out.seq = 0;
    for (int i = 0; i < 8; ++i) out.seq |= static_cast<std::uint64_t>(d[1 + i]) << (i * 8);
    std::uint32_t plen = 0;
    for (int i = 0; i < 4; ++i) plen |= static_cast<std::uint32_t>(d[9 + i]) << (i * 8);
    if (plen > n - 13) return false;
    out.payload.assign(d + 13, d + 13 + plen);
    return true;
}

// ============================================================================
// 测试用例
// ============================================================================

TEST(TcpChannel, BasicSendRecv) {
    ZmqRouterFixture srv;
    ASSERT_TRUE(srv.ready()) << "ZMQ router fixture failed to bind";

    TcpChannelConfig cfg;
    cfg.connect_uri = srv.endpoint();
    cfg.connect_timeout = std::chrono::milliseconds(2000);
    cfg.io_timeout = std::chrono::milliseconds(2000);
    TcpChannel chan(cfg);
    EXPECT_TRUE(chan.is_connected());

    // 在服务端线程接收一帧
    std::thread srv_thread([&] {
        auto raw = srv.recv_raw(2000);
        ASSERT_FALSE(raw.empty());
        DecodedFrame f;
        ASSERT_TRUE(decode_frame_bytes(raw.data(), raw.size(), f));
        EXPECT_EQ(f.priority, MessagePriority::Data);
        EXPECT_GT(f.seq, 0u);
        EXPECT_EQ(f.payload.size(), 5u);
        EXPECT_EQ(std::memcmp(f.payload.data(), "hello", 5), 0);

        // 回复一帧
        std::uint8_t resp[13 + 5] = {};
        resp[0] = static_cast<std::uint8_t>(MessagePriority::Control);
        for (int i = 0; i < 8; ++i) resp[1 + i] = static_cast<std::uint8_t>(i + 1);
        resp[9 + 0] = 5;  // plen=5 LE
        std::memcpy(resp + 13, "world", 5);
        srv.send_raw(resp, sizeof(resp));
    });

    // 客户端发送
    MessageFrame out;
    out.payload.assign({'h','e','l','l','o'});
    out.priority = MessagePriority::Data;
    auto sr = chan.send_base(std::move(out));
    EXPECT_EQ(sr, SendResult::Ok);

    // 客户端接收
    MessageFrame in;
    auto rs = chan.recv_base(in, 2000);
    EXPECT_EQ(rs, RecvStatus::Ok);
    EXPECT_EQ(in.priority, MessagePriority::Control);
    EXPECT_EQ(in.payload.size(), 5u);
    EXPECT_EQ(std::memcmp(in.payload.data(), "world", 5), 0);

    srv_thread.join();
    chan.close();
}

TEST(TcpChannel, SendToClosedReturnsClosed) {
    TcpChannelConfig cfg;
    cfg.connect_uri = "tcp://127.0.0.1:1";  // 无监听
    cfg.connect_timeout = std::chrono::milliseconds(100);
    cfg.io_timeout = std::chrono::milliseconds(100);
    TcpChannel chan(cfg);

    chan.close();

    MessageFrame m;
    m.payload.assign({'x'});
    auto sr = chan.send_base(std::move(m));
    EXPECT_EQ(sr, SendResult::Closed);
}

TEST(TcpChannel, RecvTimeoutReturnsTimeout) {
    ZmqRouterFixture srv;
    ASSERT_TRUE(srv.ready());

    TcpChannelConfig cfg;
    cfg.connect_uri = srv.endpoint();
    cfg.io_timeout = std::chrono::milliseconds(2000);
    TcpChannel chan(cfg);

    MessageFrame in;
    auto rs = chan.recv_base(in, 100);  // 100ms 超时
    EXPECT_EQ(rs, RecvStatus::Timeout);
    chan.close();
}

TEST(TcpChannel, HeartbeatBypassesBackpressure) {
    // HEARTBEAT 消息使用 ZMQ_DONTWAIT 标志，因此即使 HWM=1（队列立即满），
    // 第二次/第三次 send_base 也会**立即**返回，不会阻塞。
    // 这才是 "bypasses backpressure" 的真实语义：发送端不阻塞，而非绕过 HWM 上限。
    ZmqRouterFixture srv;
    ASSERT_TRUE(srv.ready());

    TcpChannelConfig cfg;
    cfg.connect_uri = srv.endpoint();
    cfg.io_timeout = std::chrono::milliseconds(5000);
    cfg.send_hwm = 1;  // 极低 HWM
    TcpChannel chan(cfg);

    std::thread srv_thread([&] {
        // 接收方仅 drain 一次，剩余 heartbeat 留在队列/被丢弃
        (void)srv.recv_raw(2000);
    });

    // 连续发送 5 个 heartbeat：第一个 Ok，后续因 HWM=1 返回 Full
    // 关键断言：每次 send 都**立即**返回（总耗时 ≪ io_timeout）
    auto t_start = std::chrono::steady_clock::now();
    std::size_t ok_count = 0;
    std::size_t full_count = 0;
    for (int i = 0; i < 5; ++i) {
        MessageFrame m;
        m.payload.assign({static_cast<std::uint8_t>(i)});
        m.priority = MessagePriority::Heartbeat;
        auto sr = chan.send_base(std::move(m));
        if (sr == SendResult::Ok) ++ok_count;
        else if (sr == SendResult::Full) ++full_count;
        else FAIL() << "unexpected SendResult at iteration " << i;
    }
    auto elapsed = std::chrono::steady_clock::now() - t_start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GT(ok_count, 0u) << "expected at least one Ok send";
    EXPECT_GT(full_count, 0u) << "expected HWM to trigger Full for subsequent sends";
    // DONTWAIT 语义：5 次 send 合计 < 100ms（远比 io_timeout=5000ms 短）
    EXPECT_LT(elapsed_ms, 100) << "heartbeat sends should not block on HWM, got " << elapsed_ms << "ms";

    srv_thread.join();
    chan.close();
}

TEST(TcpChannel, InvalidUriFailsConnect) {
    TcpChannelConfig cfg;
    cfg.connect_uri = "invalid-not-a-uri";
    cfg.io_timeout = std::chrono::milliseconds(100);
    TcpChannel chan(cfg, /*auto_connect=*/false);
    EXPECT_FALSE(chan.connect());
    EXPECT_EQ(chan.last_error(), udaf::core::ErrorCode::NET_CONNECTION_REFUSED);
    chan.close();
}

TEST(TcpChannel, TruncatedFrameReturnsError) {
    ZmqRouterFixture srv;
    ASSERT_TRUE(srv.ready());

    TcpChannelConfig cfg;
    cfg.connect_uri = srv.endpoint();
    cfg.io_timeout = std::chrono::milliseconds(2000);
    TcpChannel chan(cfg);

    std::thread srv_thread([&] {
        (void)srv.recv_raw(2000);  // 接收客户端首发
        // 发送一个长度不足 13 字节的非法帧
        std::uint8_t garbage[5] = {1, 2, 3, 4, 5};
        srv.send_raw(garbage, sizeof(garbage));
    });

    // 客户端发送任意 payload 以触发 ROUTER 接收 + identity 转发
    MessageFrame m;
    m.payload.assign({'a'});
    (void)chan.send_base(std::move(m));

    MessageFrame in;
    auto rs = chan.recv_base(in, 2000);
    EXPECT_EQ(rs, RecvStatus::Error);
    EXPECT_EQ(chan.last_error(), udaf::core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);

    srv_thread.join();
    chan.close();
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}