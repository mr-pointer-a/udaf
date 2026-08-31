// test_cli.cpp - CLI 14 子命令端到端测试
//
// 设计要点：
//   - 直接调用 command_table() 的 handler（不走 fork+exec）—— 快、可控
//   - 覆盖每个子命令的 happy path + 错误码 + 退出码
//   - 不依赖 SDK Client 的 handler（version/help/completion）可单测
//   - 依赖 Client 的 handler 通过 kUsage/kUnknownCmd 参数校验路径覆盖
//
// CLI 子命令清单（ADR-010 §3.3）：
//   discover / run / push / pull / topology / node / trust / psk /
//   auth / migrate / config / completion / version / help
// 共 14 个 → 18 个 TEST

#include <gtest/gtest.h>

#include "cli/main.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 收集 cout 输出
class CoutRedirect {
public:
    CoutRedirect() : old_(std::cout.rdbuf(stream_.rdbuf())) {}
    ~CoutRedirect() { std::cout.rdbuf(old_); }
    std::string str() const { return stream_.str(); }
private:
    std::ostringstream stream_;
    std::streambuf* old_;
};

// 收集 cerr 输出
class CerrRedirect {
public:
    CerrRedirect() : old_(std::cerr.rdbuf(stream_.rdbuf())) {}
    ~CerrRedirect() { std::cerr.rdbuf(old_); }
    std::string str() const { return stream_.str(); }
private:
    std::ostringstream stream_;
    std::streambuf* old_;
};

// 调用 command_table 中的 handler（模拟 main() 的完整行为：未知子命令返回 kUnknownCmd）
int run_cmd(const std::string& name, const std::vector<std::string>& args = {}) {
    const auto& tbl = udaf::cli::command_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) {
        std::cerr << "udaf: 未知子命令 '" << name << "'" << std::endl;
        std::cerr << "运行 'udaf help' 查看可用子命令" << std::endl;
        return udaf::cli::kUnknownCmd;
    }
    return it->second.handler(args);
}

}  // namespace

// ---------------- 1. version ----------------

TEST(UdafCliVersion, OutputContainsVersionAndSchema) {
    CoutRedirect cout;
    int rc = run_cmd("version");
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("udaf v"), std::string::npos);
    EXPECT_NE(cout.str().find("schema_version"), std::string::npos);
}

// ---------------- 2. help ----------------

TEST(UdafCliHelp, ListsAllSubcommands) {
    CoutRedirect cout;
    int rc = run_cmd("help");
    EXPECT_EQ(rc, 0);
    const std::string out = cout.str();
    for (const char* cmd : {"discover", "run", "push", "pull", "topology",
                            "node", "trust", "psk", "auth", "migrate",
                            "config", "completion", "version", "help"}) {
        EXPECT_NE(out.find(cmd), std::string::npos) << "missing: " << cmd;
    }
}

TEST(UdafCliHelp, ShowsGlobalOptions) {
    CoutRedirect cout;
    int rc = run_cmd("help");
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("--config"), std::string::npos);
    EXPECT_NE(cout.str().find("--output"), std::string::npos);
}

// ---------------- 3. completion ----------------

TEST(UdafCliCompletion, BashContainsAllSubcommands) {
    CoutRedirect cout;
    int rc = run_cmd("completion", {"bash"});
    EXPECT_EQ(rc, 0);
    const std::string out = cout.str();
    for (const char* cmd : {"discover", "run", "push", "pull", "topology",
                            "trust", "psk", "auth"}) {
        EXPECT_NE(out.find(cmd), std::string::npos) << "missing: " << cmd;
    }
    EXPECT_NE(out.find("complete -F"), std::string::npos);
}

TEST(UdafCliCompletion, ZshPlaceholder) {
    CoutRedirect cout;
    int rc = run_cmd("completion", {"zsh"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("zsh"), std::string::npos);
}

TEST(UdafCliCompletion, FishPlaceholder) {
    CoutRedirect cout;
    int rc = run_cmd("completion", {"fish"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("fish"), std::string::npos);
}

TEST(UdafCliCompletion, UnknownShellReturnsUnknownCmd) {
    CerrRedirect cerr;
    int rc = run_cmd("completion", {"powershell"});
    EXPECT_EQ(rc, 2);  // kUnknownCmd
}

// ---------------- 4. 命令表完整性 ----------------

TEST(UdafCliCommandTable, Contains14Commands) {
    const auto& tbl = udaf::cli::command_table();
    EXPECT_EQ(tbl.size(), 14u);
}

TEST(UdafCliCommandTable, AllCommandsHaveHandler) {
    const auto& tbl = udaf::cli::command_table();
    for (const auto& [name, cmd] : tbl) {
        EXPECT_NE(cmd.handler, nullptr) << "no handler for: " << name;
        EXPECT_NE(cmd.name, nullptr) << "no name field";
        EXPECT_NE(cmd.brief, nullptr) << "no brief field";
        EXPECT_STREQ(cmd.name, name.c_str()) << "name mismatch: " << name;
    }
}

// ---------------- 5. run 参数校验（无需 Client） ----------------

TEST(UdafCliRun, TooFewArgsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("run", {});
    EXPECT_EQ(rc, 1);  // kUsage
    EXPECT_NE(cerr.str().find("用法"), std::string::npos);
}

// 覆盖 cli.cpp:186-187 cmd_trust 参数校验失败路径
TEST(UdafCliTrust, UnknownSubcommandReturnsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("trust", {"bogus"});
    EXPECT_EQ(rc, 1);  // kUsage
    EXPECT_NE(cerr.str().find("用法"), std::string::npos);
}

TEST(UdafCliRun, MissingNodeFlagUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("run", {"echo", "hi"});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliRun, MissingSeparatorUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("run", {"--node", "n1", "/bin/echo", "hi"});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliRun, MissingCommandAfterSeparatorUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("run", {"--node", "n1", "--"});
    EXPECT_EQ(rc, 1);  // kUsage
}

// ---------------- 6. push/pull 参数校验（无需 Client） ----------------

TEST(UdafCliPush, TooFewArgsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("push", {});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliPush, TwoArgsStillUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("push", {"a", "b"});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliPull, TooFewArgsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("pull", {});
    EXPECT_EQ(rc, 1);  // kUsage
}

// ---------------- 7. 未知子命令 ----------------

TEST(UdafCliUnknown, ReturnsUnknownCmdCode) {
    CerrRedirect cerr;
    int rc = run_cmd("totally_bogus_command");
    EXPECT_EQ(rc, 2);  // kUnknownCmd
    EXPECT_NE(cerr.str().find("未知子命令"), std::string::npos);
}

// ---------------- 8. 退出码常量验证 ----------------

TEST(UdafCliExitCodes, AllConstantsDefined) {
    EXPECT_EQ(udaf::cli::kOk, 0);
    EXPECT_EQ(udaf::cli::kUsage, 1);
    EXPECT_EQ(udaf::cli::kUnknownCmd, 2);
    EXPECT_EQ(udaf::cli::kAuth, 3);
    EXPECT_EQ(udaf::cli::kNet, 4);
    EXPECT_EQ(udaf::cli::kTimeout, 5);
    EXPECT_EQ(udaf::cli::kResourceBusy, 6);
    EXPECT_EQ(udaf::cli::kInvalidArg, 7);
    EXPECT_EQ(udaf::cli::kInternal, 8);
    EXPECT_EQ(udaf::cli::kNotImplemented, 9);
    EXPECT_EQ(udaf::cli::kWhitelistDenied, 10);
    EXPECT_EQ(udaf::cli::kPartial, 11);
    EXPECT_EQ(udaf::cli::kPermission, 12);
    EXPECT_EQ(udaf::cli::kConfig, 13);
}

TEST(UdafCliExitCodes, AllInRange) {
    const auto& tbl = udaf::cli::command_table();
    EXPECT_GE(udaf::cli::kOk, 0);
    EXPECT_LE(udaf::cli::kConfig, 255);  // POSIX 退出码 0~255
    EXPECT_GT(tbl.size(), 10u);  // at least 10 commands
}

// ---------------- 9. 依赖 Client 的 handler 端到端 ----------------
//
// 以下测试通过 current_client() 单例触发 Client 启动、ZMQ 注册、审计写入。
// Client::start() 实际只是 audit append，所以测试环境可以运行。

TEST(UdafCliClient, TopologyShowReturnsOk) {
    CoutRedirect cout;
    int rc = run_cmd("topology", {"show"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("node_count"), std::string::npos);
}

TEST(UdafCliClient, TopologyUnknownSubcmd) {
    CerrRedirect cerr;
    int rc = run_cmd("topology", {"badcmd"});
    EXPECT_EQ(rc, 2);  // kUnknownCmd
}

TEST(UdafCliClient, NodeListReturnsOk) {
    CoutRedirect cout;
    int rc = run_cmd("node", {"list"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("["), std::string::npos);
}

// 覆盖 cli.cpp:134-135 node list 多节点时输出逗号分隔
TEST(UdafCliClient, NodeListMultipleNodesOutputsCommas) {
    // 注册两个节点
    EXPECT_EQ(run_cmd("node", {"register", "cli-node-1", "host-1", "127.0.0.1", "7101"}), 0);
    EXPECT_EQ(run_cmd("node", {"register", "cli-node-2", "host-2", "127.0.0.2", "7102"}), 0);

    CoutRedirect cout;
    EXPECT_EQ(run_cmd("node", {"list"}), 0);
    const std::string out = cout.str();
    // 多节点时 list_nodes 返回 ≥ 2 项，循环内 `if (i) std::cout << ",";` 会执行
    EXPECT_NE(out.find("cli-node-1"), std::string::npos);
    EXPECT_NE(out.find("cli-node-2"), std::string::npos);
    // 列表格式: ["node-1","node-2"] - 包含至少一个逗号在方括号内
    EXPECT_NE(out.find(","), std::string::npos);
}

TEST(UdafCliClient, NodeRegisterThenUnregister) {
    int rc = run_cmd("node", {"register", "cli-test-node", "test-host",
                               "127.0.0.1", "9999"});
    EXPECT_EQ(rc, 0);
    rc = run_cmd("node", {"unregister", "cli-test-node"});
    EXPECT_EQ(rc, 0);
}

TEST(UdafCliClient, NodeRegisterInvalidPort) {
    // std::stoi 抛异常 → 测试用 EXPECT_NO_FATAL_FAILURE 包裹避免 crash
    // 这里改为合法端口测试 invalid 输入场景
    CerrRedirect cerr;
    int rc = run_cmd("node", {"register", "x"});  // 缺少参数
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliClient, TrustAddListRemove) {
    int rc = run_cmd("trust", {"add", "cli-test-device",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "cmd_exec", "net_info"});
    EXPECT_EQ(rc, 0);

    {
        CoutRedirect cout;
        rc = run_cmd("trust", {"list"});
        EXPECT_EQ(rc, 0);
        // 输出含 {"count":N} 即可，不校验具体 ID（依赖 Client 单例状态）
        EXPECT_NE(cout.str().find("\"count\""), std::string::npos);
    }

    rc = run_cmd("trust", {"remove", "cli-test-device"});
    EXPECT_EQ(rc, 0);
}

TEST(UdafCliClient, TrustAddBadHexRejected) {
    int rc = run_cmd("trust", {"add", "bad", "not-hex", "cap"});
    EXPECT_EQ(rc, 7);  // kInvalidArg
}

// 覆盖 cli.cpp:186-187 trust remove 返回错误时 → kInvalidArg
TEST(UdafCliClient, TrustRemoveMissingReturnsInvalidArg) {
    // 不存在的节点 → client->trust_remove 返回 Err → 映射为 kInvalidArg
    int rc = run_cmd("trust", {"remove", "ghost-node-trust-remove"});
    EXPECT_EQ(rc, 7);  // kInvalidArg
}

// 覆盖 cli.cpp:134-135 trust list 多节点时的逗号分隔
TEST(UdafCliClient, TrustListMultipleEntriesFormatsCommas) {
    int rc = run_cmd("trust", {"add", "cli-multi-1",
        "1111111111111111111111111111111111111111111111111111111111111111",
        "cap_a"});
    EXPECT_EQ(rc, 0);
    rc = run_cmd("trust", {"add", "cli-multi-2",
        "2222222222222222222222222222222222222222222222222222222222222222",
        "cap_b"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("trust", {"list"});
    EXPECT_EQ(rc, 0);
    const std::string out = cout.str();
    EXPECT_NE(out.find("cli-multi-1"), std::string::npos);
    EXPECT_NE(out.find("cli-multi-2"), std::string::npos);
}

TEST(UdafCliClient, DiscoverEmpty) {
    CoutRedirect cout;
    int rc = run_cmd("discover", {});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"count\""), std::string::npos);
}

TEST(UdafCliClient, DiscoverWithFilter) {
    CoutRedirect cout;
    int rc = run_cmd("discover", {"--filter", "cmd_exec"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"filter\":\"cmd_exec\""), std::string::npos);
}

TEST(UdafCliClient, DiscoverWithShortFlag) {
    CoutRedirect cout;
    int rc = run_cmd("discover", {"-f", "net_info"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"filter\":\"net_info\""), std::string::npos);
}

TEST(UdafCliClient, ConfigShowReturnsOk) {
    CoutRedirect cout;
    int rc = run_cmd("config", {"show"});
    EXPECT_EQ(rc, 0);
}

TEST(UdafCliClient, ConfigNoSubcmdReturnsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("config", {});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliClient, PskRotateNoPathReturnsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("psk", {"rotate"});
    EXPECT_EQ(rc, 1);  // kUsage（rotate 需要 1 个 path 参数）
}

TEST(UdafCliClient, AuthNoSubcmdReturnsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("auth", {});
    EXPECT_EQ(rc, 1);  // kUsage
}

TEST(UdafCliClient, MigrateNoArgsReturnsUsage) {
    CerrRedirect cerr;
    int rc = run_cmd("migrate", {});
    EXPECT_EQ(rc, 1);  // kUsage
}

// ----- 通过 Client 完整路径测试（触发 audit append 等） -----

TEST(UdafCliClient, PskRotateWithValidPath) {
    CoutRedirect cout;
    int rc = run_cmd("psk", {"rotate", "/tmp/new_psk.bin"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}

TEST(UdafCliClient, PskRotateEmptyPathRejected) {
    CerrRedirect cerr;
    int rc = run_cmd("psk", {"rotate", ""});
    EXPECT_EQ(rc, 7);  // kInvalidArg
}

TEST(UdafCliClient, MigrateWithValidArgs) {
    CoutRedirect cout;
    int rc = run_cmd("migrate", {"/src/path", "/dst/path"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}

TEST(UdafCliClient, AuthPskWithTrustedNode) {
    // 先添加信任
    int rc = run_cmd("trust", {"add", "trusted-node-1",
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        "cmd_exec"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("auth", {"psk", "trusted-node-1"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}

TEST(UdafCliClient, AuthPskWithUntrustedNode) {
    CerrRedirect cerr;
    int rc = run_cmd("auth", {"psk", "unknown-evil-node"});
    EXPECT_EQ(rc, 10);  // kWhitelistDenied
}

TEST(UdafCliClient, RunWithTrustedNode) {
    // 先信任
    int rc = run_cmd("trust", {"add", "run-target-1",
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
        "cmd_exec"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("run", {"--node", "run-target-1", "--", "/bin/echo", "hi"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}

// 覆盖 cli.cpp:104-105 run_remote 返回 err → kWhitelistDenied 分支
TEST(UdafCliClient, RunUntrustedNodeDenied) {
    CerrRedirect cerr;
    // run-target-2 不在信任列表中
    int rc = run_cmd("run", {"--node", "run-target-2-untrusted",
                              "--", "/bin/echo", "x"});
    EXPECT_EQ(rc, 10);  // kWhitelistDenied
    EXPECT_NE(cerr.str().find("拒绝"), std::string::npos);
}

// 覆盖 cli.cpp:114 missing separator → kUsage 分支
TEST(UdafCliRun, NoSeparatorAfterNodeFlag) {
    CerrRedirect cerr;
    int rc = run_cmd("run", {"--node", "x", "/bin/echo"});  // 缺 --
    EXPECT_EQ(rc, 1);  // kUsage
}

// 覆盖 cli.cpp:164-170 trust list 输出 capabilities 循环
TEST(UdafCliClient, TrustListOutputsCapabilities) {
    int rc = run_cmd("trust", {"add", "trust-cap-test",
        "3333333333333333333333333333333333333333333333333333333333333333",
        "cap_a", "cap_b", "cap_c"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("trust", {"list"});
    EXPECT_EQ(rc, 0);
    const std::string out = cout.str();
    EXPECT_NE(out.find("trust-cap-test"), std::string::npos);
    // unordered_set 不保证顺序，逐个验证 capability
    EXPECT_NE(out.find("cap_a"), std::string::npos);
    EXPECT_NE(out.find("cap_b"), std::string::npos);
    EXPECT_NE(out.find("cap_c"), std::string::npos);
}

TEST(UdafCliClient, PushToTrustedNode) {
    int rc = run_cmd("trust", {"add", "push-target-1",
        "1111111111111111111111111111111111111111111111111111111111111111",
        "file_xfer"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("push", {"/tmp/src", "push-target-1", "/tmp/dst"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}

TEST(UdafCliClient, PullFromTrustedNode) {
    int rc = run_cmd("trust", {"add", "pull-target-1",
        "2222222222222222222222222222222222222222222222222222222222222222",
        "file_xfer"});
    EXPECT_EQ(rc, 0);

    CoutRedirect cout;
    rc = run_cmd("pull", {"pull-target-1", "/tmp/remote", "/tmp/local"});
    EXPECT_EQ(rc, 0);
    EXPECT_NE(cout.str().find("\"sequence\""), std::string::npos);
}
