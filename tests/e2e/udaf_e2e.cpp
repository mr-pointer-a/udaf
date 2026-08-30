// udaf_e2e.cpp - 端到端集成程序
// 单进程内：register 2 nodes → trust add → discover → topology → run_remote → audit
// 编译为可执行：./udaf_e2e

#include "sdk/sdk/sdk.hpp"
#include "audit/audit.hpp"
#include "ability_a/registry/service_registry.hpp"

#include <cstdio>
#include <iostream>
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

    // 4) Topology
    auto topo = client.topology_summary();
    std::printf("[4] topology nodes=%zu\n", topo.node_count);

    // 5) Run on device-2 (denied)
    auto denied = client.run_remote("device-2", "/bin/echo", {"hi"});
    std::printf("[5] run on device-2 (denied) is_err=%d err=%d\n",
                denied.is_err(),
                denied.is_err() ? (int)denied.error() : 0);

    // 6) Run on device-1 (allowed)
    auto allowed = client.run_remote("device-1", "/bin/echo", {"hello"});
    std::printf("[6] run on device-1 (allowed) seq=%llu\n",
                allowed.is_ok() ? (unsigned long long)allowed.value() : 0);

    // 7) Push file to device-1
    auto push = client.push_file("/etc/hostname", "device-1", "/tmp/h.txt");
    std::printf("[7] push to device-1 seq=%llu\n",
                push.is_ok() ? (unsigned long long)push.value() : 0);

    // 8) Config show
    std::printf("[8] config: %s\n", client.config_show().c_str());

    // 9) Audit sequence
    std::printf("[9] audit sequence = %llu\n",
                (unsigned long long)client.sequence());

    client.stop();
    std::printf("=== DONE ===\n");
    return 0;
}