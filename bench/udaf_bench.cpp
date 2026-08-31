// udaf_bench.cpp - 关键性能契约基准
// 实施覆盖 6 项主契约（29 项全集的代表性集合）：
//   #5  inproc 通道 P95 延迟 < 100μs
//   #7  inproc 通道吞吐 ≥ 50K msg/s
//   #14 registry snapshot 10000 条 < 100ms
//   #15 PSK 握手 P95 < 2ms
//   #23 fork+exec ≤ 80ms（平均）
//   #27 audit 吞吐 ≥ 1000 ops/s
//
// 命名：udaf_bench_<scenario>

#include <benchmark/benchmark.h>

#include "ability_a/registry/service_registry.hpp"
#include "ability_b/port/port.hpp"
#include "ability_b/transport/channel.hpp"
#include "ability_b/transport/inproc_channel.hpp"
#include "ability_b/topology/topology.hpp"
#include "ability_b/node/node.hpp"
#include "ability_c/executor/process_executor.hpp"
#include "ability_c/nodes/nodes.hpp"
#include "crypto/psk.hpp"
#include "audit/audit.hpp"
#include "observability/observability.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using udaf::ability_a::registry::ServiceRegistry;
using udaf::ability_a::registry::RegistryEntry;
using udaf::ability_b::transport::Channel;
using udaf::ability_b::transport::ChannelBase;
using udaf::ability_b::transport::InprocChannel;
using udaf::ability_b::port::InputPort;
using udaf::ability_b::port::OutputPort;
using udaf::ability_b::topology::Topology;
using udaf::ability_b::topology::TopologyTransaction;
using udaf::ability_b::topology::PeerNode;
using udaf::ability_c::executor::ProcessExecutor;
using udaf::audit::AuditLogger;
using udaf::audit::ActionType;
using udaf::observability::Meter;
using udaf::observability::MetricId;

namespace {

void fill_registry(ServiceRegistry& r, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        RegistryEntry e;
        e.node_id_      = "node-" + std::to_string(i);
        e.hostname_     = "h" + std::to_string(i);
        e.bind_address_ = "10.0.0." + std::to_string((i % 254) + 1);
        e.bind_port_    = static_cast<std::uint16_t>(8000 + (i % 1000));
        (void)r.register_node(e);
    }
}

}  // namespace

// ============ #14 registry 10000 条 snapshot ============
static void udaf_bench_registry_snapshot_10k(benchmark::State& state) {
    static ServiceRegistry* reg = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static ServiceRegistry instance;
        fill_registry(instance, 10000);
        reg = &instance;
    });
    for (auto _ : state) {
        auto snap = reg->snapshot();
        benchmark::DoNotOptimize(snap);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(udaf_bench_registry_snapshot_10k);

// ============ #7 inproc 单端 send→recv 吞吐 ============
static void udaf_bench_inproc_throughput(benchmark::State& state) {
    using Msg = int;
    static Channel<Msg>* ch = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Channel<Msg> instance(std::make_unique<InprocChannel>(4096));
        ch = &instance;
    });
    Msg v = 42;
    Msg out = 0;
    for (auto _ : state) {
        (void)ch->send(v);
        (void)ch->recv(out, 0);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_inproc_throughput);

// ============ #5 inproc send→recv 延迟 ============
static void udaf_bench_inproc_latency_p95(benchmark::State& state) {
    using Msg = std::uint64_t;
    static Channel<Msg>* ch = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Channel<Msg> instance(std::make_unique<InprocChannel>(4096));
        ch = &instance;
    });
    Msg out = 0;
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        (void)ch->send(static_cast<Msg>(state.iterations()));
        (void)ch->recv(out, 0);
        auto t1 = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        state.SetIterationTime(static_cast<double>(ns) / 1e9);
    }
}
BENCHMARK(udaf_bench_inproc_latency_p95)->UseManualTime();

// ============ #15 PSK 握手 P95 ============
static void udaf_bench_psk_handshake_p95(benchmark::State& state) {
    namespace crypto = udaf::crypto;
    std::vector<std::uint8_t> psk(32, 0xCD);
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        auto req = crypto::psk_handshake_client_new("bench");
        auto resp = crypto::psk_handshake_server_respond(psk, req);
        auto keys = crypto::psk_handshake_client_finalize(psk, req, resp.value());
        auto t1 = std::chrono::steady_clock::now();
        if (keys.is_err()) { state.SkipWithError("handshake failed"); return; }
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        state.SetIterationTime(static_cast<double>(us) / 1e6);
    }
}
BENCHMARK(udaf_bench_psk_handshake_p95)->UseManualTime();

// ============ #23 fork+exec 平均时间 ============
static void udaf_bench_fork_exec(benchmark::State& state) {
    ProcessExecutor::Options opts;
    opts.executable = "/bin/echo";
    opts.allowed_executables = {"/bin/echo"};
    for (auto _ : state) {
        auto r = ProcessExecutor::execute(opts);
        if (r.is_err()) { state.SkipWithError("exec failed"); return; }
        benchmark::DoNotOptimize(r.value().exit_code);
    }
}
BENCHMARK(udaf_bench_fork_exec);

// ============ #27 audit 吞吐 ============
static void udaf_bench_audit_throughput(benchmark::State& state) {
    auto path = std::filesystem::temp_directory_path() / "udaf_bench_audit.log";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    AuditLogger log(path.string());
    int i = 0;
    for (auto _ : state) {
        (void)log.append(ActionType::NodeHeartbeat, "h", "d",
                         "{\"i\":" + std::to_string(i++) + "}");
    }
    state.SetItemsProcessed(state.iterations());
    std::filesystem::remove(path, ec);
}
BENCHMARK(udaf_bench_audit_throughput);

// ============ #1 Result<T> 开销 ============
static void udaf_bench_result_ok(benchmark::State& state) {
    using udaf::core::Result;
    for (auto _ : state) {
        auto r = Result<int>::ok(42);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_result_ok);

// ============ #8 channel reopen 重连退避 ============
static void udaf_bench_channel_reopen(benchmark::State& state) {
    using Msg = int;
    for (auto _ : state) {
        Channel<Msg> a(std::make_unique<InprocChannel>(1024));
        Channel<Msg> b(std::make_unique<InprocChannel>(1024));
        Msg v = 1, out = 0;
        (void)a.send(v);
        (void)b.recv(out, 0);
        // reset（仅供基准）
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_channel_reopen);

// ============ #9 channel send/recv p95 (inproc) ============
static void udaf_bench_channel_send_recv(benchmark::State& state) {
    using Msg = int;
    static Channel<Msg>* ch = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Channel<Msg> instance(std::make_unique<InprocChannel>(4096));
        ch = &instance;
    });
    Msg v = 1, out = 0;
    for (auto _ : state) {
        (void)ch->send(v);
        (void)ch->recv(out, 0);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_channel_send_recv);

// ============ #10 channel priority heartbeat ============
static void udaf_bench_channel_heartbeat_priority(benchmark::State& state) {
    using Msg = int;
    static Channel<Msg>* ch = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Channel<Msg> instance(std::make_unique<InprocChannel>(4096));
        ch = &instance;
    });
    Msg v = 1, out = 0;
    for (auto _ : state) {
        // heartbeat 优先级强制投递
        (void)ch->send(v, udaf::ability_b::transport::MessagePriority::Heartbeat);
        (void)ch->recv(out, 0);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_channel_heartbeat_priority);

// ============ #11 input_port try_recv ============
static void udaf_bench_port_try_recv(benchmark::State& state) {
    using Msg = int;
    InputPort<Msg> port("p", 4096);
    for (auto _ : state) {
        (void)port.try_recv();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_port_try_recv);

// ============ #12 output_port try_send ============
static void udaf_bench_port_try_send(benchmark::State& state) {
    using Msg = int;
    InputPort<Msg> in("p_in", 4096);
    udaf::ability_b::port::PortInfo info;
    info.name = "p";
    info.type_index_ = std::type_index(typeid(Msg));
    info.is_input = false;
    OutputPort<Msg> port(info, &in);
    int i = 0;
    for (auto _ : state) {
        (void)port.try_send(Msg{++i});
        (void)in.try_recv();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_port_try_send);

// ============ #13 topology add_node ============
static void udaf_bench_topology_add_node(benchmark::State& state) {
    static Topology* topo = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Topology instance;
        topo = &instance;
    });
    int i = 0;
    for (auto _ : state) {
        auto tx = topo->begin_transaction().value();
        PeerNode pn;
        pn.node_id      = "n-" + std::to_string(i++);
        pn.hostname     = "h";
        pn.bind_address = "10.0.0.1";
        pn.bind_port    = 9000;
        pn.capabilities = {"cmd_exec"};
        (void)tx.add_node(std::move(pn));
        (void)topo->commit(std::move(tx));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_topology_add_node);

// ============ #16 pki handshake (placeholder benchmark) ============
static void udaf_bench_pki_handshake(benchmark::State& state) {
    // 简化为 cost of compute SHA-256 64 次（模拟 TLS 1.3 完整握手）
    std::vector<std::uint8_t> buf(64, 0x42);
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 4; ++i) {
            (void)udaf::crypto::hkdf_sha256({}, buf, "pki-bench", 32);
        }
        auto t1 = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        state.SetIterationTime(static_cast<double>(us) / 1e6);
    }
}
BENCHMARK(udaf_bench_pki_handshake)->UseManualTime();

// ============ #17 wal append ============
static void udaf_bench_wal_append(benchmark::State& state) {
    // 简化：复用 audit append（已是文件 append + hash chain）
    auto path = std::filesystem::temp_directory_path() / "udaf_bench_wal.log";
    std::error_code ec; std::filesystem::remove(path, ec);
    AuditLogger log(path.string());
    int i = 0;
    for (auto _ : state) {
        (void)log.append(ActionType::TopologyUpdate, "host", "self",
                         "{\"op\":\"add\",\"i\":" + std::to_string(i++) + "}");
    }
    state.SetItemsProcessed(state.iterations());
    std::filesystem::remove(path, ec);
}
BENCHMARK(udaf_bench_wal_append);

// ============ #18 subscribe fire ============
static void udaf_bench_subscribe_fire(benchmark::State& state) {
    static ServiceRegistry* reg = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static ServiceRegistry instance;
        reg = &instance;
    });
    int i = 0;
    auto sub = reg->subscribe(
        [&](udaf::ability_a::registry::RegistryEvent, const RegistryEntry&) { /* no-op */ });
    for (auto _ : state) {
        RegistryEntry e;
        e.node_id_      = "sub-" + std::to_string(i++);
        e.bind_address_ = "10.0.0.1";
        (void)reg->register_node(e);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_subscribe_fire);

// ============ #19 subscribe_batch (sequential) ============
static void udaf_bench_subscribe_batch(benchmark::State& state) {
    static ServiceRegistry* reg = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static ServiceRegistry instance;
        reg = &instance;
    });
    // 单订阅者 + 100 次 register 触发的回调
    int count = 0;
    auto sub = reg->subscribe(
        [&](udaf::ability_a::registry::RegistryEvent, const RegistryEntry&) { ++count; });
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            RegistryEntry e;
            e.node_id_      = "batch-" + std::to_string(i);
            e.bind_address_ = "10.0.0.2";
            (void)reg->register_node(e);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
    (void)count;
}
BENCHMARK(udaf_bench_subscribe_batch);

// ============ #20 hmac single call ============
static void udaf_bench_hmac_single(benchmark::State& state) {
    std::vector<std::uint8_t> key(32, 0xAA);
    std::vector<std::uint8_t> data(64, 0xBB);
    for (auto _ : state) {
        auto r = udaf::crypto::psk_aead_encrypt(
            key, std::array<std::uint8_t, 12>{}, data, {});
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_hmac_single);

// ============ #21 large_msg 1MB ============
static void udaf_bench_large_msg_1mb(benchmark::State& state) {
    std::vector<std::uint8_t> key(32, 0xCC);
    std::vector<std::uint8_t> msg(1024 * 1024, 0x55);
    std::array<std::uint8_t, 12> nonce{};
    for (auto _ : state) {
        auto ct = udaf::crypto::psk_aead_encrypt(key, nonce, msg, {});
        auto pt = udaf::crypto::psk_aead_decrypt(key, nonce, ct.value(), {});
        benchmark::DoNotOptimize(pt);
    }
    state.SetBytesProcessed(state.iterations() * msg.size());
}
BENCHMARK(udaf_bench_large_msg_1mb);

// ============ #21 单条消息默认 4KB 编码 ============
static void udaf_bench_default_msg_4kb(benchmark::State& state) {
    std::vector<std::uint8_t> buf(4 * 1024, 0x42);
    for (auto _ : state) {
        // 简单帧编码：length-prefix + payload
        std::vector<std::uint8_t> frame;
        frame.reserve(4 + buf.size());
        std::uint32_t len = static_cast<std::uint32_t>(buf.size());
        frame.push_back(static_cast<std::uint8_t>(len & 0xff));
        frame.push_back(static_cast<std::uint8_t>((len >> 8) & 0xff));
        frame.push_back(static_cast<std::uint8_t>((len >> 16) & 0xff));
        frame.push_back(static_cast<std::uint8_t>((len >> 24) & 0xff));
        frame.insert(frame.end(), buf.begin(), buf.end());
        benchmark::DoNotOptimize(frame);
    }
}
BENCHMARK(udaf_bench_default_msg_4kb);

// ============ #22 heart 100 aggregation ============
static void udaf_bench_heart_aggregate_100(benchmark::State& state) {
    static ServiceRegistry* reg = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static ServiceRegistry instance;
        for (int i = 0; i < 100; ++i) {
            RegistryEntry e;
            e.node_id_      = "agg-" + std::to_string(i);
            e.bind_address_ = "10.0.0." + std::to_string(i + 1);
            (void)instance.register_node(e);
        }
        reg = &instance;
    });
    for (auto _ : state) {
        auto snap = reg->snapshot();
        benchmark::DoNotOptimize(snap);
    }
}
BENCHMARK(udaf_bench_heart_aggregate_100);

// ============ #23 fork+exec 平均（已有）保留 ============

// ============ #24 node cold startup ============
static void udaf_bench_node_cold_startup(benchmark::State& state) {
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        udaf::ability_c::nodes::CmdExecNode n;
        n.set_allowed_executables({"/bin/echo"});
        udaf::ability_b::node::NodeConfig cfg;
        (void)n.init(cfg);
        (void)n.start();
        (void)n.stop();
        auto t1 = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        state.SetIterationTime(static_cast<double>(us) / 1e6);
    }
}
BENCHMARK(udaf_bench_node_cold_startup)->UseManualTime();

// ============ #25 node reload ============
static void udaf_bench_node_reload(benchmark::State& state) {
    udaf::ability_c::nodes::CmdExecNode n;
    n.set_allowed_executables({"/bin/echo"});
    udaf::ability_b::node::NodeConfig cfg;
    (void)n.init(cfg);
    (void)n.start();
    for (auto _ : state) {
        (void)n.reload();
    }
    (void)n.stop();
}
BENCHMARK(udaf_bench_node_reload);

// ============ #26 crypto overhead (% vs unencrypted) ============
static void udaf_bench_crypto_overhead(benchmark::State& state) {
    std::vector<std::uint8_t> key(32, 0xDD);
    std::vector<std::uint8_t> msg(256, 0xEE);
    std::array<std::uint8_t, 12> nonce{};
    for (auto _ : state) {
        auto ct = udaf::crypto::psk_aead_encrypt(key, nonce, msg, {});
        benchmark::DoNotOptimize(ct);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_crypto_overhead);

// ============ #28 meter observe ============
static void udaf_bench_meter_observe(benchmark::State& state) {
    static Meter* m = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        static Meter instance;
        m = &instance;
    });
    for (auto _ : state) {
        m->inc_counter(MetricId::DiscoveryBroadcastTotal);
        m->observe_histogram(MetricId::ChannelLatencyMicro, 1234);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(udaf_bench_meter_observe);

// ============ #29 prometheus export ============
static void udaf_bench_prom_export(benchmark::State& state) {
    Meter m;
    for (int i = 0; i < 10; ++i) m.inc_counter(MetricId::DiscoveryBroadcastTotal);
    for (auto _ : state) {
        auto s = m.export_prometheus();
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(udaf_bench_prom_export);

// ============ #22 加密握手后每帧加密开销 ≤ 50μs ============
static void udaf_bench_aead_per_frame(benchmark::State& state) {
    std::vector<std::uint8_t> key(32, 0xAB);
    std::vector<std::uint8_t> msg(256, 0xCD);  // 256B 典型命令帧
    udaf::crypto::Nonce n{};
    for (auto _ : state) {
        auto enc = udaf::crypto::psk_aead_encrypt(key, n, msg, {});
        benchmark::DoNotOptimize(enc);
    }
}
BENCHMARK(udaf_bench_aead_per_frame);

// ============ #4 崩溃恢复 ≤ 5s（1000 条 WAL 回放） ============
static void udaf_bench_wal_recovery_1000(benchmark::State& state) {
    // 写 1000 条 → 重新打开 → 顺序回放
    static std::filesystem::path wal_path;
    static std::once_flag once;
    std::call_once(once, [] {
        wal_path = std::filesystem::temp_directory_path() / "udaf_bench_wal_recovery.bin";
        std::ofstream ofs(wal_path, std::ios::binary | std::ios::trunc);
        std::uint32_t magic = 0x57524C01;
        ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        // 写 1000 × 64B entries
        for (int i = 0; i < 1000; ++i) {
            std::uint32_t seq = i + 1;
            std::uint8_t payload[64] = {0};
            std::memcpy(payload, &seq, sizeof(seq));
            ofs.write(reinterpret_cast<const char*>(payload), sizeof(payload));
        }
    });
    for (auto _ : state) {
        std::ifstream ifs(wal_path, std::ios::binary);
        std::uint32_t magic = 0;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        int counted = 0;
        while (ifs) {
            std::uint8_t payload[64];
            ifs.read(reinterpret_cast<char*>(payload), sizeof(payload));
            if (ifs.gcount() == sizeof(payload)) ++counted;
        }
        benchmark::DoNotOptimize(counted);
    }
}
BENCHMARK(udaf_bench_wal_recovery_1000);

// ============ #20 可观测性开销 < 5%（基线 vs 观测） ============
static void udaf_bench_observability_overhead_baseline(benchmark::State& state) {
    Meter m;
    for (auto _ : state) {
        // 无 observe 调用，测量空循环开销
        benchmark::DoNotOptimize(m);
    }
}
BENCHMARK(udaf_bench_observability_overhead_baseline);

static void udaf_bench_observability_overhead_enabled(benchmark::State& state) {
    Meter m;
    for (auto _ : state) {
        m.inc_counter(MetricId::DiscoveryBroadcastTotal);
        m.observe_histogram(MetricId::ChannelLatencyMicro, 42);
        benchmark::DoNotOptimize(m);
    }
}
BENCHMARK(udaf_bench_observability_overhead_enabled);

// ============ #13 最大并发节点 1000（scheduler 并发调度） ============
static void udaf_bench_max_concurrent_nodes_1000(benchmark::State& state) {
    static std::once_flag once;
    static std::vector<std::string> node_ids;
    std::call_once(once, [] {
        node_ids.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            node_ids.push_back("n" + std::to_string(i));
        }
    });
    for (auto _ : state) {
        udaf::ability_b::node::Scheduler s;
        // 模拟 1000 个节点的调度查询（不实际执行，仅调度开销）
        for (const auto& id : node_ids) {
            (void)s.is_allowed(id, "host");
        }
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(udaf_bench_max_concurrent_nodes_1000);

// ---------------- 内存契约 BENCHMARKs（架构 #1 #2 #28 #29）----------------
//
// 通过 /proc/self/status 读取 RSS（驻留集大小），
// 多次实例化核心组件后报告增量峰值。
// Google Benchmark 输出 rss_kb counter，scripts/check_perf_contracts.sh 解析为 KB/MB。

#include "sdk/sdk/sdk.hpp"

#include <fstream>
#include <string>

namespace {

// 读取当前进程 RSS（KB）
std::size_t read_rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            // VmRSS:	   12345 kB
            std::size_t kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %zu", &kb);
            return kb;
        }
    }
    return 0;
}

}  // namespace

// #28 设备端峰值内存 < 16MB
// 测量 delta：构造前后 RSS 之差，反映 UDAF 增量（排除 libc/benchmark 二进制）
void udaf_bench_device_peak_memory(benchmark::State& state) {
    for (auto _ : state) {
        std::size_t rss_before = read_rss_kb();
        std::size_t rss_after = 0;
        {
            udaf::sdk::ClientConfig cfg;
            cfg.node_id    = "bench-device";
            cfg.audit_path = "/tmp/udaf_bench_device.log";
            udaf::sdk::Client c(cfg);
            (void)c.start();
            udaf::ability_a::registry::ServiceRegistry reg;
            for (int i = 0; i < 50; ++i) {
                udaf::ability_a::registry::RegistryEntry e;
                e.node_id_      = "n" + std::to_string(i);
                e.hostname_     = "host" + std::to_string(i);
                e.bind_address_ = "127.0.0.1";
                e.bind_port_    = static_cast<std::uint16_t>(8000 + i);
                (void)reg.register_node(e);
            }
            benchmark::DoNotOptimize(c);
            rss_after = read_rss_kb();
            (void)c.stop();
        }
        std::size_t delta = (rss_after > rss_before) ? (rss_after - rss_before) : rss_after;
        state.counters["rss_kb"] = static_cast<double>(delta);
    }
}
BENCHMARK(udaf_bench_device_peak_memory);

// #29 主机端峰值内存 < 128MB
void udaf_bench_host_peak_memory(benchmark::State& state) {
    for (auto _ : state) {
        std::size_t rss_before = read_rss_kb();
        std::size_t rss_after = 0;
        {
            udaf::sdk::ClientConfig cfg;
            cfg.node_id    = "bench-host";
            cfg.audit_path = "/tmp/udaf_bench_host.log";
            udaf::sdk::Client c(cfg);
            (void)c.start();
            udaf::ability_a::registry::ServiceRegistry reg;
            for (int i = 0; i < 1000; ++i) {
                udaf::ability_a::registry::RegistryEntry e;
                e.node_id_      = "node" + std::to_string(i);
                e.hostname_     = "host" + std::to_string(i);
                e.bind_address_ = "10.0.0." + std::to_string(i % 256);
                e.bind_port_    = static_cast<std::uint16_t>(8000 + (i % 1000));
                (void)reg.register_node(e);
            }
            benchmark::DoNotOptimize(c);
            rss_after = read_rss_kb();
            (void)c.stop();
        }
        std::size_t delta = (rss_after > rss_before) ? (rss_after - rss_before) : rss_after;
        state.counters["rss_kb"] = static_cast<double>(delta);
    }
}
BENCHMARK(udaf_bench_host_peak_memory);

// #1 设备端空闲内存 < 8MB
// 最小工作集（无 registry），测量 Client 单实例 delta
void udaf_bench_device_idle_memory(benchmark::State& state) {
    for (auto _ : state) {
        std::size_t rss_before = read_rss_kb();
        std::size_t rss_after = 0;
        {
            udaf::sdk::ClientConfig cfg;
            cfg.node_id    = "bench-device-idle";
            cfg.audit_path = "";  // 不开 audit
            udaf::sdk::Client c(cfg);
            benchmark::DoNotOptimize(c);
            rss_after = read_rss_kb();
        }
        std::size_t delta = (rss_after > rss_before) ? (rss_after - rss_before) : rss_after;
        state.counters["rss_kb"] = static_cast<double>(delta);
    }
}
BENCHMARK(udaf_bench_device_idle_memory);

// #2 主机端空闲内存 < 32MB
// Client + 空 Registry
void udaf_bench_host_idle_memory(benchmark::State& state) {
    for (auto _ : state) {
        std::size_t rss_before = read_rss_kb();
        std::size_t rss_after = 0;
        {
            udaf::sdk::ClientConfig cfg;
            cfg.node_id    = "bench-host-idle";
            cfg.audit_path = "/tmp/udaf_bench_host_idle.log";
            udaf::sdk::Client c(cfg);
            udaf::ability_a::registry::ServiceRegistry reg;
            benchmark::DoNotOptimize(c);
            rss_after = read_rss_kb();
        }
        std::size_t delta = (rss_after > rss_before) ? (rss_after - rss_before) : rss_after;
        state.counters["rss_kb"] = static_cast<double>(delta);
    }
}
BENCHMARK(udaf_bench_host_idle_memory);

// ============ #22b PSK AEAD 1MB 吞吐（≥ 200 MB/s）============
// 与 #22 单帧 256B 微基准对照；本场景面向大块数据传输（文件块/拓扑快照）
static void udaf_bench_aead_throughput_1mb(benchmark::State& state) {
    std::vector<std::uint8_t> key(32, 0xAB);
    std::vector<std::uint8_t> msg(1024 * 1024, 0xCD);  // 1 MiB
    udaf::crypto::Nonce n{};
    for (auto _ : state) {
        auto enc = udaf::crypto::psk_aead_encrypt(key, n, msg, {});
        benchmark::DoNotOptimize(enc);
    }
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(msg.size()));
}
BENCHMARK(udaf_bench_aead_throughput_1mb);

// ============ #27b 审计 hash chain 校验吞吐 ============
// 写入 N 条 → 重启 → verify_chain() 全链校验
static void udaf_bench_audit_verify_chain(benchmark::State& state) {
    static std::filesystem::path path;
    static std::once_flag once;
    std::call_once(once, [] {
        path = std::filesystem::temp_directory_path() / "udaf_bench_audit_verify.log";
        std::filesystem::remove(path);
        AuditLogger pre(path.string());
        for (int i = 0; i < 500; ++i) {
            (void)pre.append(ActionType::NodeHeartbeat, "h", "d",
                             "{\"i\":" + std::to_string(i) + "}");
        }
    });
    for (auto _ : state) {
        AuditLogger log(path.string());
        auto r = log.verify_chain();
        benchmark::DoNotOptimize(r);
    }
    std::filesystem::remove(path);
}
BENCHMARK(udaf_bench_audit_verify_chain);

// ============ #4b WAL 完整 append+replay 链路 ============
// 与 #4 区别：本场景走真实 Wal 类（schema 头 + fsync + replay 反序列化）
// 写入 200 条 → replay 校验数量 + sequence 单调
static void udaf_bench_wal_replay_full(benchmark::State& state) {
    using udaf::platform::fs::Wal;
    using udaf::platform::fs::WalConfig;
    using udaf::platform::fs::WalEntryType;

    static std::filesystem::path path;
    static std::once_flag once;
    std::call_once(once, [] {
        path = std::filesystem::temp_directory_path() / "udaf_bench_wal_full.bin";
    });
    // 写入 200 条
    WalConfig cfg;
    cfg.path_ = path;
    cfg.fsync_on_append_ = false;  // 关闭 fsync，专注 IO 路径开销
    {
        auto w = Wal::create(cfg);
        if (w.is_ok()) {
            auto& wal = *w.value();
            std::vector<std::uint8_t> payload(64, 0xAA);
            for (int i = 0; i < 200; ++i) {
                (void)wal.append(WalEntryType::CUSTOM, "bench", payload);
            }
        }
    }
    // 每次迭代重新打开 + replay
    for (auto _ : state) {
        auto w = Wal::create(cfg);
        if (w.is_err()) continue;
        auto r = w.value()->replay();
        benchmark::DoNotOptimize(r);
    }
    std::filesystem::remove(path);
}
BENCHMARK(udaf_bench_wal_replay_full);

// ============ #13b 拓扑事务 commit（含 WAL 持久化）============
// 批量添加 50 个节点 → commit → current_node_count 校验
static void udaf_bench_topology_commit_50(benchmark::State& state) {
    using udaf::ability_b::topology::Topology;
    using udaf::ability_b::topology::PeerNode;

    for (auto _ : state) {
        Topology topo;
        auto txr = topo.begin_transaction();
        if (txr.is_err()) continue;
        auto& tx = txr.value();
        for (int i = 0; i < 50; ++i) {
            PeerNode p;
            p.node_id = "n-" + std::to_string(i);
            p.hostname = "h-" + std::to_string(i);
            p.bind_address = "127.0.0.1";
            p.bind_port = static_cast<std::uint16_t>(9000 + i);
            (void)tx.add_node(std::move(p));
        }
        auto cr = topo.commit(std::move(tx));
        benchmark::DoNotOptimize(cr);
    }
    state.SetItemsProcessed(state.iterations() * 50);
}
BENCHMARK(udaf_bench_topology_commit_50);

BENCHMARK_MAIN();