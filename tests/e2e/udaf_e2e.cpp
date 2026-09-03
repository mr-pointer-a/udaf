// udaf_e2e.cpp - 端到端集成程序
// 单进程内：register → trust → discover → topology(事务) → node spawn → run_remote → audit
// 编译为可执行：./udaf_e2e

#include "sdk/sdk/sdk.hpp"
#include "audit/audit.hpp"
#include "ability_a/registry/service_registry.hpp"
#include "ability_b/topology/topology.hpp"
#include "ability_c/nodes/nodes.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace as = udaf::audit;

int main(int argc, char** argv) {
    std::string tmp = "/tmp/udaf_e2e.log";
    if (argc > 1) tmp = argv[1];

    std::printf("=== UDAF E2E Demo ===\n");
    std::printf("audit: %s\n", tmp.c_str());

    udaf::sdk::ClientConfig cfg;
    cfg.node_id    = "e2e-host";
    cfg.audit_path = tmp;
    udaf::sdk::Client client(cfg);
    if (client.start().is_err()) {
        std::fprintf(stderr, "start failed\n");
        return 1;
    }

    // 1) Register 2 nodes
    auto r1 = client.register_node("device-1", "host1", "10.0.0.1", 9001);
    auto r2 = client.register_node("device-2", "host2", "10.0.0.2", 9002);
    std::printf("[1] register device-1=%s device-2=%s\n",
                r1.is_ok() && r1.value() ? "added" : "no",
                r2.is_ok() && r2.value() ? "added" : "no");

    // 2) Trust device-1
    auto t = client.trust_add("device-1",
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
        {"cmd_exec"});
    std::printf("[2] trust add device-1=%s\n",
                t.is_ok() && t.value() ? "added" : "updated");

    // 3) Discover
    auto entries = client.discover("");
    std::printf("[3] discover count=%zu\n", entries.size());

    // 4) Client topology_summary
    auto topo = client.topology_summary();
    std::printf("[4] topology (client) nodes=%zu\n", topo.node_count);

    // 5) Topology transaction: add_node + add_edge + remove_node
    {
        auto tx_opt = udaf::ability_b::topology::Topology::begin_transaction();
        if (tx_opt.is_err()) {
            std::fprintf(stderr, "[5] begin_transaction failed\n");
            return 1;
        }
        auto tx = std::move(tx_opt.value());

        udaf::ability_b::topology::PeerNode n1;
        n1.node_id = "topo-node-a";
        n1.hostname = "topo-host-a";
        n1.bind_address = "192.168.1.10";
        n1.bind_port = 9100;
        n1.capabilities = {"dataflow", "compute"};
        tx.add_node(std::move(n1));

        udaf::ability_b::topology::PeerNode n2;
        n2.node_id = "topo-node-b";
        n2.hostname = "topo-host-b";
        n2.bind_address = "192.168.1.11";
        n2.bind_port = 9101;
        n2.capabilities = {"dataflow"};
        tx.add_node(std::move(n2));

        tx.add_edge(udaf::ability_b::topology::PeerEdge{
            "topo-node-a", "topo-node-b", "tcp"});

        auto commit_ret = client.topology_commit(std::move(tx));
        std::printf("[5] topology tx add_node(2)+add_edge(1) commit=%s\n",
                    commit_ret.is_ok() ? "ok" : "fail");
    }

    // 6) Remove topo-node-a via topology transaction
    {
        auto tx_opt = udaf::ability_b::topology::Topology::begin_transaction();
        if (tx_opt.is_err()) {
            std::fprintf(stderr, "[6] begin_transaction failed\n");
            return 1;
        }
        auto tx = std::move(tx_opt.value());
        tx.remove_node("topo-node-a");

        auto commit_ret = client.topology_commit(std::move(tx));
        std::printf("[6] topology tx remove_node(topo-node-a) commit=%s\n",
                    commit_ret.is_ok() ? "ok" : "fail");
    }

    // 7) Node spawn: create CmdExecNode and manage its lifecycle
    {
        auto node = std::make_unique<udaf::ability_c::nodes::CmdExecNode>();
        udaf::ability_b::node::NodeConfig nc;
        nc.node_id = "cmd-exec-spawned";
        nc.worker_threads = 1;

        auto init_ret = node->init(nc);
        std::printf("[7a] CmdExecNode init=%s\n",
                    init_ret.is_ok() ? "ok" : "fail");

        if (init_ret.is_ok()) {
            auto start_ret = node->start();
            std::printf("[7b] CmdExecNode start=%s state=%s\n",
                        start_ret.is_ok() ? "ok" : "fail",
                        udaf::ability_b::node::to_string(node->state()));

            if (start_ret.is_ok()) {
                auto stop_ret = node->stop();
                std::printf("[7c] CmdExecNode stop=%s state=%s\n",
                            stop_ret.is_ok() ? "ok" : "fail",
                            udaf::ability_b::node::to_string(node->state()));
            }
        }
    }

    // 8) Run on device-2 (denied)
    auto denied = client.run_remote("device-2", "/bin/echo", {"hi"});
    std::printf("[8] run on device-2 (denied) is_err=%d err=%d\n",
                denied.is_err(),
                denied.is_err() ? static_cast<int>(denied.error()) : 0);

    // 9) Run on device-1 (allowed)
    auto allowed = client.run_remote("device-1", "/bin/echo", {"hello"});
    std::printf("[9] run on device-1 (allowed) seq=%llu\n",
                allowed.is_ok() ? static_cast<unsigned long long>(allowed.value()) : 0ULL);

    // 10) Push file to device-1
    auto push = client.push_file("/etc/hostname", "device-1", "/tmp/h.txt");
    std::printf("[10] push to device-1 seq=%llu\n",
                push.is_ok() ? static_cast<unsigned long long>(push.value()) : 0ULL);

    // 11) Config show
    std::printf("[11] config: %s\n", client.config_show().c_str());

    // 12) Audit sequence
    std::printf("[12] audit sequence = %llu\n",
                static_cast<unsigned long long>(client.sequence()));

    (void)client.stop();
    std::printf("=== DONE ===\n");
    return 0;
}