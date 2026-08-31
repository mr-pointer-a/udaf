// cli.cpp - CLI 14 子命令 handler + command_table + Client 单例
//
// 拆分原因：
//   - main.cpp 仅保留 main() 入口
//   - 所有 handler + command_table 在此编译成 udaf_cli 库
//   - 测试 test_cli 链接 udaf_cli 直接调用 handler（无需 fork+exec）

#include "cli/main.hpp"

#include "sdk/sdk/sdk.hpp"
#include "audit/audit.hpp"
#include "ability_a/registry/service_registry.hpp"
#include "core/error_code.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

namespace {

udaf::sdk::Client* current_client() noexcept;

int cmd_help(const std::vector<std::string>& /*args*/) {
    std::cout <<
        "udaf - Unified Device & Application Framework\n"
        "\n"
        "用法: udaf <subcommand> [options]\n"
        "\n"
        "子命令:\n"
        "  discover     发现网络节点（按 capability 过滤）\n"
        "  run          在远端设备执行命令\n"
        "  push         推送文件\n"
        "  pull         拉取文件\n"
        "  topology     查看/导出拓扑（YAML/JSON）\n"
        "  node         节点管理（list/register/unregister）\n"
        "  trust        白名单管理（add/remove/list）\n"
        "  psk          PSK 派生与轮转\n"
        "  auth         认证握手（psk/pki）\n"
        "  migrate      数据迁移\n"
        "  config       配置文件管理\n"
        "  completion   输出 shell completion 脚本\n"
        "  version      输出版本号\n"
        "  help         本帮助\n"
        "\n"
        "全局选项:\n"
        "  --config <path>     配置文件\n"
        "  --node-id <id>      当前节点 ID\n"
        "  --output <fmt>      输出格式：text|json|yaml（默认 text）\n"
        "  --verbose           详细日志\n"
        "  --quiet             静默\n"
        "  --no-color          禁用 ANSI 颜色\n";
    return udaf::cli::kOk;
}

int cmd_version(const std::vector<std::string>& /*args*/) {
    std::cout << "udaf v0.1.0\n";
    std::cout << "schema_version 1\n";
    std::cout << "stage E (SDK + CLI)\n";
    return udaf::cli::kOk;
}

int cmd_discover(const std::vector<std::string>& args) {
    std::string filter;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--filter" || args[i] == "-f") filter = args[i + 1];
    }
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    auto entries = client->discover(filter);
    std::cout << "{\"count\":" << entries.size() << ",\"filter\":\""
              << filter << "\"}";
    std::cout << std::endl;
    return udaf::cli::kOk;
}

int cmd_run(const std::vector<std::string>& args) {
    if (args.size() < 3 || args[0] != "--node") {
        std::cerr << "用法: udaf run --node <id> -- <cmd> [args...]" << std::endl;
        return udaf::cli::kUsage;
    }
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    const std::string& node_id = args[1];
    // 解析 -- <cmd> <args>
    if (args[2] != "--") {
        std::cerr << "udaf run: 期望 '--' 分隔" << std::endl;
        return udaf::cli::kUsage;
    }
    if (args.size() < 4) {
        std::cerr << "udaf run: 缺少命令" << std::endl;
        return udaf::cli::kUsage;
    }
    const std::string& command = args[3];
    std::vector<std::string> cmd_args(args.begin() + 4, args.end());
    auto r = client->run_remote(node_id, command, cmd_args);
    if (r.is_err()) {
        std::cerr << "udaf run: 拒绝（白名单 / 校验失败）" << std::endl;
        return udaf::cli::kWhitelistDenied;
    }
    std::cout << "{\"sequence\":" << r.value() << ",\"node\":\"" << node_id
              << "\",\"command\":\"" << command << "\"}" << std::endl;
    return udaf::cli::kOk;
}

int cmd_topology(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    auto sum = client->topology_summary();
    if (!args.empty() && args[0] == "show") {
        std::cout << "nodes:" << std::endl;
        for (const auto& n : sum.nodes) std::cout << "  - " << n << std::endl;
        std::cout << "node_count: " << sum.node_count
                  << "\nedge_count: " << sum.edge_count << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "udaf topology: 未知子命令" << std::endl;
    return udaf::cli::kUnknownCmd;
}

int cmd_node(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (!args.empty() && args[0] == "list") {
        auto nodes = client->list_nodes();
        std::cout << "[";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (i) std::cout << ",";
            std::cout << "\"" << nodes[i] << "\"";
        }
        std::cout << "]" << std::endl;
        return udaf::cli::kOk;
    }
    if (args.size() >= 5 && args[0] == "register") {
        auto r = client->register_node(args[1], args[2], args[3],
                                        static_cast<std::uint16_t>(std::stoi(args[4])));
        if (r.is_err()) return udaf::cli::kInvalidArg;
        std::cout << "{\"registered\":" << (r.value() ? "true" : "updated") << "}" << std::endl;
        return udaf::cli::kOk;
    }
    if (args.size() >= 2 && args[0] == "unregister") {
        auto r = client->unregister_node(args[1]);
        if (r.is_err()) return udaf::cli::kInvalidArg;
        std::cout << "{\"unregistered\":" << (r.value() ? "true" : "false") << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf node <list|register <id> <host> <addr> <port>|unregister <id>>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_trust(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (!args.empty() && args[0] == "list") {
        auto list = client->trust_list();
        std::cout << "{\"count\":" << list.size() << "}" << std::endl;
        for (const auto& t : list) {
            std::cout << "  " << t.node_id
                      << "  cap=";
            for (std::size_t i = 0; i < t.capabilities.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << t.capabilities[i];
            }
            std::cout << std::endl;
        }
        return udaf::cli::kOk;
    }
    if (args.size() >= 4 && args[0] == "add") {
        std::vector<std::string> caps(args.begin() + 3, args.end());
        auto r = client->trust_add(args[1], args[2], caps);
        if (r.is_err()) return udaf::cli::kInvalidArg;
        std::cout << "{\"added\":" << (r.value() ? "true" : "updated") << "}" << std::endl;
        return udaf::cli::kOk;
    }
    if (args.size() >= 2 && args[0] == "remove") {
        auto r = client->trust_remove(args[1]);
        if (r.is_err()) return udaf::cli::kInvalidArg;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf trust <list|add <id> <fp-hex> [cap...]|remove <id>>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_psk(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (args.size() >= 2 && args[0] == "rotate") {
        auto r = client->psk_rotate(args[1]);
        if (r.is_err()) return udaf::cli::kInvalidArg;
        std::cout << "{\"sequence\":" << r.value() << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf psk rotate <new-psk-path>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_auth(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (args.size() >= 2 && args[0] == "psk") {
        auto r = client->auth_psk(args[1]);
        if (r.is_err()) return udaf::cli::kWhitelistDenied;
        std::cout << "{\"sequence\":" << r.value() << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf auth psk <node-id>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_migrate(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (args.size() >= 2) {
        auto r = client->migrate(args[0], args[1]);
        if (r.is_err()) return udaf::cli::kInvalidArg;
        std::cout << "{\"sequence\":" << r.value() << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf migrate <src> <dst>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_config(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (!args.empty() && args[0] == "show") {
        std::cout << client->config_show() << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf config show" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_push(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (args.size() >= 3) {
        auto r = client->push_file(args[0], args[1], args[2]);
        if (r.is_err()) return udaf::cli::kWhitelistDenied;
        std::cout << "{\"sequence\":" << r.value() << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf push <src> <dst-node> <dst-path>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_pull(const std::vector<std::string>& args) {
    auto* client = current_client();
    if (!client) return udaf::cli::kInternal;
    if (args.size() >= 3) {
        auto r = client->pull_file(args[0], args[1], args[2]);
        if (r.is_err()) return udaf::cli::kWhitelistDenied;
        std::cout << "{\"sequence\":" << r.value() << "}" << std::endl;
        return udaf::cli::kOk;
    }
    std::cerr << "用法: udaf pull <src-node> <src-path> <dst>" << std::endl;
    return udaf::cli::kUsage;
}

int cmd_completion(const std::vector<std::string>& args) {
    const std::string shell = args.empty() ? std::string("bash") : args[0];
    if (shell == "bash") {
        std::cout <<
            "_udaf_completion() {\n"
            "  local cur=${COMP_WORDS[COMP_CWORD]}\n"
            "  COMPREPLY=( $(compgen -W \"discover run push pull topology node"
            " trust psk auth migrate config completion version help\""
            " -- $cur) )\n"
            "}\n"
            "complete -F _udaf_completion udaf\n";
        return udaf::cli::kOk;
    }
    if (shell == "zsh" || shell == "fish") {
        std::cout << "# " << shell << " completion: 阶段 E4 占位" << std::endl;
        return udaf::cli::kOk;
    }
    return udaf::cli::kUnknownCmd;
}

// 14 子命令注册表（在匿名 namespace 内，供 cli 内部使用）
const std::unordered_map<std::string, udaf::cli::Command>& cli_table() {
    static const std::unordered_map<std::string, udaf::cli::Command> tbl = {
        {"discover",   {"discover",   cmd_discover,   "发现网络节点"}},
        {"run",        {"run",        cmd_run,        "在远端执行命令"}},
        {"push",       {"push",       cmd_push,       "推送文件"}},
        {"pull",       {"pull",       cmd_pull,       "拉取文件"}},
        {"topology",   {"topology",   cmd_topology,   "查看拓扑"}},
        {"node",       {"node",       cmd_node,       "节点管理"}},
        {"trust",      {"trust",      cmd_trust,      "白名单管理"}},
        {"psk",        {"psk",        cmd_psk,        "PSK 管理"}},
        {"auth",       {"auth",       cmd_auth,       "认证握手"}},
        {"migrate",    {"migrate",    cmd_migrate,    "数据迁移"}},
        {"config",     {"config",     cmd_config,     "配置管理"}},
        {"completion", {"completion", cmd_completion, "shell completion"}},
        {"version",    {"version",    cmd_version,    "版本号"}},
        {"help",       {"help",       cmd_help,       "帮助"}},
    };
    return tbl;
}

// 单例 Client（CLI 全局生命周期）
static std::unique_ptr<udaf::sdk::Client>& global_client() {
    static std::unique_ptr<udaf::sdk::Client> c;
    return c;
}

udaf::sdk::Client* current_client() noexcept {
    auto& c = global_client();
    if (!c) {
        udaf::sdk::ClientConfig cfg;
        cfg.node_id    = "cli-" + std::to_string(::getpid());
        cfg.audit_path = "/tmp/udaf_cli_" + cfg.node_id + ".log";
        c = std::make_unique<udaf::sdk::Client>(cfg);
        (void)c->start();
    }
    return c.get();
}

}  // namespace

// 公共接口：暴露给测试与 main.cpp
namespace udaf::cli {
const std::unordered_map<std::string, Command>& command_table() noexcept {
    return cli_table();
}
}  // namespace udaf::cli
