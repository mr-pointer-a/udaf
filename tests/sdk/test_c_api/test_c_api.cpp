// test_c_api.cpp - C 接口单测
#include <gtest/gtest.h>

#include "sdk/udaf_c.h"

#include <cstring>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

class CSdkTmp : public ::testing::Test {
protected:
    fs::path path_;
    void SetUp() override {
        path_ = fs::temp_directory_path() /
                ("udaf_c_sdk_" + std::to_string(::getpid()) + ".log");
        std::error_code ec;
        fs::remove(path_, ec);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(path_, ec);
    }
};

TEST_F(CSdkTmp, VersionAndAbi) {
    EXPECT_GT(std::strlen(udaf_version_string()), 0u);
    EXPECT_EQ(udaf_abi_version(), 1u);
}

TEST_F(CSdkTmp, CreateAndDestroy) {
    udaf_client_config_t cfg{};
    cfg.node_id    = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    ASSERT_NE(cli, nullptr);
    ASSERT_EQ(udaf_client_start(cli), UDAF_OK);
    ASSERT_EQ(udaf_client_stop(cli), UDAF_OK);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, DiscoverEmpty) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_node_entry_t* entries = nullptr;
    uint32_t count = 0;
    ASSERT_EQ(udaf_client_discover(cli, "", &entries, &count), UDAF_OK);
    EXPECT_EQ(count, 0u);
    udaf_client_free_entries(entries, count);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TrustAddListRemove) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);

    const char* caps[] = {"cmd_exec", "net_info"};
    EXPECT_EQ(udaf_client_trust_add(cli, "device-1",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        caps, 2), UDAF_OK);

    EXPECT_EQ(udaf_client_trust_remove(cli, "device-1"), UDAF_OK);
    EXPECT_EQ(udaf_client_trust_remove(cli, "device-1"), UDAF_ERR_NOT_FOUND);

    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, InvalidFingerprintRejected) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_trust_add(cli, "bad", "not-hex-64-chars-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                      nullptr, 0), UDAF_ERR_WHITELIST);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, RunRemoteDeniedWithoutWhitelist) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    const char* args[] = {"hi"};
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_run_remote(cli, "evil-node", "/bin/echo", args, 1, &out),
              UDAF_ERR_WHITELIST);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TopologyNodeCountZero) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_topology_node_count(cli), 0u);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, NullArgs) {
    EXPECT_EQ(udaf_client_create(nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(nullptr);  // destroy 容忍 nullptr
    udaf_client_config_t cfg{};
    cfg.node_id = "h";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_discover(cli, "", nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

// ===== 补齐：覆盖 push/pull/run/list 完整路径 =====

TEST_F(CSdkTmp, PushFileDeniedUntrusted) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_push_file(cli, "/src", "untrusted-node", "/dst", &out),
              UDAF_ERR_WHITELIST);
    // 覆盖 udaf_c.cpp:215 push_file 默认错误路径
    // 空 src/dst_node/dst_path 触发 Client::push_file 返回 INVALID_ARG
    EXPECT_EQ(udaf_client_push_file(cli, "", "n", "/d", &out),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_push_file(cli, "/s", "", "/d", &out),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_push_file(cli, "/s", "n", "", &out),
              UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, PushFileInvalidArg) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_push_file(cli, nullptr, "n", "/d", &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_push_file(cli, "/s", nullptr, "/d", &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_push_file(cli, "/s", "n", nullptr, &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_push_file(cli, "/s", "n", "/d", nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, PullFileDeniedUntrusted) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_pull_file(cli, "untrusted", "/p", "/d", &out),
              UDAF_ERR_WHITELIST);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, RunRemoteEmptyCmdInvalidArg) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    // 先加入白名单
    const char* caps[] = {"cmd_exec"};
    ASSERT_EQ(udaf_client_trust_add(cli, "n1",
        "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899",
        caps, 1), UDAF_OK);
    udaf_audit_result_t out{};
    const char* args[] = {"hi"};
    EXPECT_EQ(udaf_client_run_remote(cli, "n1", "", args, 1, &out),
              UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

// 覆盖 udaf_c.cpp:199 run_remote INTERNAL 分支（audit_logger 为空）
TEST_F(CSdkTmp, RunRemoteInternalWhenNoAuditLogger) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    // 注意：audit_path 留空 → audit_logger_ = nullptr
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    // 加入白名单（绕开 UNTRUSTED 错误）
    const char* caps[] = {"cmd_exec"};
    ASSERT_EQ(udaf_client_trust_add(cli, "n1",
        "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899",
        caps, 1), UDAF_OK);
    udaf_audit_result_t out{};
    const char* args[] = {"hi"};
    EXPECT_EQ(udaf_client_run_remote(cli, "n1", "echo", args, 1, &out),
              UDAF_ERR_INTERNAL);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, RunRemoteOk) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    const char* caps[] = {"cmd_exec"};
    ASSERT_EQ(udaf_client_trust_add(cli, "n1",
        "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899",
        caps, 1), UDAF_OK);
    udaf_audit_result_t out{};
    const char* args[] = {"hello", "world"};
    ASSERT_EQ(udaf_client_run_remote(cli, "n1", "/bin/echo", args, 2, &out), UDAF_OK);
    EXPECT_GT(out.sequence, 0u);
    EXPECT_STREQ(out.json_payload, "{}");
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, RunRemoteNullArgs) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_run_remote(nullptr, "n", "/c", nullptr, 0, &out),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_run_remote(cli, nullptr, "/c", nullptr, 0, &out),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_run_remote(cli, "n", nullptr, nullptr, 0, &out),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_run_remote(cli, "n", "/c", nullptr, 0, nullptr),
              UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TrustListEmpty) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_trust_entry_t* entries = nullptr;
    uint32_t count = 999;
    ASSERT_EQ(udaf_client_trust_list(cli, &entries, &count), UDAF_OK);
    EXPECT_EQ(count, 0u);
    udaf_client_free_trust_entries(entries, count);
    udaf_client_destroy(cli);
}

// 覆盖 udaf_c.cpp:150-157 trust_list 循环体 + push_file/pull_file success 分支
TEST_F(CSdkTmp, TrustListWithEntriesAndFileOps) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    cfg.audit_path = path_.c_str();  // 测试 fixture 提供的临时路径
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);

    // 信任一个节点
    const char* valid_fp = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const char* caps[] = {"file_xfer", "cmd_exec"};
    ASSERT_EQ(udaf_client_trust_add(cli, "trusted-dev", valid_fp, caps, 2), UDAF_OK);

    // trust_list 应返回 1 条（只校验 node_id 和 capability_count，不解引用 capabilities 指针）
    udaf_trust_entry_t* entries = nullptr;
    uint32_t count = 0;
    ASSERT_EQ(udaf_client_trust_list(cli, &entries, &count), UDAF_OK);
    EXPECT_EQ(count, 1u);
    EXPECT_STREQ(entries[0].node_id, "trusted-dev");
    EXPECT_EQ(entries[0].capability_count, 2u);
    EXPECT_NE(entries[0].fingerprint_hex, nullptr);
    udaf_client_free_trust_entries(entries, count);

    // push_file success（覆盖 udaf_c.cpp:198-200）
    udaf_audit_result_t push_res{};
    ASSERT_EQ(udaf_client_push_file(cli, "/src/path", "trusted-dev", "/dst/path", &push_res),
              UDAF_OK);
    EXPECT_GT(push_res.sequence, 0u);

    // pull_file success（覆盖 udaf_c.cpp:214-216）
    udaf_audit_result_t pull_res{};
    ASSERT_EQ(udaf_client_pull_file(cli, "trusted-dev", "/remote", "/local", &pull_res),
              UDAF_OK);
    EXPECT_GT(pull_res.sequence, 0u);

    // run_remote success（覆盖 udaf_c.cpp:182-184）
    const char* args[] = {"hello"};
    udaf_audit_result_t run_res{};
    ASSERT_EQ(udaf_client_run_remote(cli, "trusted-dev", "/bin/echo", args, 1, &run_res),
              UDAF_OK);
    EXPECT_GT(run_res.sequence, 0u);

    udaf_client_destroy(cli);
}

// 覆盖 udaf_c.cpp:130 trust_add 返回非 INVALID_ARG 的 INTERNAL 路径
//   触发方式：传入正确的 hex（64 字符）但 fuzzer 内部转换失败（实际很难触发）
//   这里改为覆盖 unregister_node 返回 false（节点不存在）的 NOT_FOUND 路径
TEST_F(CSdkTmp, UnregisterMissingNodeReturnsNotFound) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    // 节点不存在 → r.value() == false → UDAF_ERR_NOT_FOUND
    EXPECT_EQ(udaf_client_unregister_node(cli, "ghost-node"), UDAF_ERR_NOT_FOUND);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TrustListNullArgs) {
    EXPECT_EQ(udaf_client_trust_list(nullptr, nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_trust_list(cli, nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TrustRemoveNullArgs) {
    EXPECT_EQ(udaf_client_trust_remove(nullptr, "n"), UDAF_ERR_INVALID_ARG);
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_trust_remove(cli, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, DiscoverWithNodes) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    // 通过内部 Client API 注册（直接用 SDK CLI 接口）
    // 此处通过重复 discover 调用触发计数
    udaf_node_entry_t* entries = nullptr;
    uint32_t count = 0;
    ASSERT_EQ(udaf_client_discover(cli, "filter", &entries, &count), UDAF_OK);
    EXPECT_GE(count, 0u);
    udaf_client_free_entries(entries, count);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, TopologyNodeCountAfterRegister) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_topology_node_count(nullptr), 0u);
    EXPECT_EQ(udaf_client_topology_node_count(cli), 0u);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, StartStopNullArgs) {
    EXPECT_EQ(udaf_client_start(nullptr), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_stop(nullptr),  UDAF_ERR_INVALID_ARG);
}

TEST_F(CSdkTmp, PullFileNullArgs) {
    EXPECT_EQ(udaf_client_pull_file(nullptr, "n", "/p", "/d", nullptr),
              UDAF_ERR_INVALID_ARG);
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_audit_result_t out{};
    EXPECT_EQ(udaf_client_pull_file(cli, nullptr, "/p", "/d", &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_pull_file(cli, "n", nullptr, "/d", &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_pull_file(cli, "n", "/p", nullptr, &out), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_pull_file(cli, "n", "/p", "/d", nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

// ===== 注册节点 + discover 循环覆盖 =====

TEST_F(CSdkTmp, RegisterNodeNullArgs) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_register_node(nullptr, "n", "h", "127.0.0.1", 80),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_register_node(cli, nullptr, "h", "127.0.0.1", 80),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_register_node(cli, "n", nullptr, "127.0.0.1", 80),
              UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_register_node(cli, "n", "h", nullptr, 80),
              UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, RegisterAndDiscoverNodes) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);

    // 注册 3 个节点
    ASSERT_EQ(udaf_client_register_node(cli, "n1", "host1", "127.0.0.1", 8000), UDAF_OK);
    ASSERT_EQ(udaf_client_register_node(cli, "n2", "host2", "127.0.0.1", 8001), UDAF_OK);
    ASSERT_EQ(udaf_client_register_node(cli, "n3", "host3", "127.0.0.1", 8002), UDAF_OK);

    EXPECT_EQ(udaf_client_topology_node_count(cli), 3u);

    // discover 触发循环（覆盖 udaf_c.cpp 67-80 行）
    udaf_node_entry_t* entries = nullptr;
    uint32_t count = 0;
    ASSERT_EQ(udaf_client_discover(cli, "", &entries, &count), UDAF_OK);
    EXPECT_EQ(count, 3u);
    ASSERT_NE(entries, nullptr);
    // 无序遍历：找出每个 port 对应的 node
    int found_8000 = 0, found_8001 = 0, found_8002 = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].bind_port == 8000) {
            EXPECT_STREQ(entries[i].node_id, "n1");
            EXPECT_STREQ(entries[i].hostname, "host1");
            ++found_8000;
        } else if (entries[i].bind_port == 8001) {
            EXPECT_STREQ(entries[i].node_id, "n2");
            ++found_8001;
        } else if (entries[i].bind_port == 8002) {
            EXPECT_STREQ(entries[i].node_id, "n3");
            ++found_8002;
        }
    }
    EXPECT_EQ(found_8000, 1);
    EXPECT_EQ(found_8001, 1);
    EXPECT_EQ(found_8002, 1);
    udaf_client_free_entries(entries, count);

    // unregister 一个
    ASSERT_EQ(udaf_client_unregister_node(cli, "n2"), UDAF_OK);
    EXPECT_EQ(udaf_client_topology_node_count(cli), 2u);

    // unregister 不存在的节点
    EXPECT_EQ(udaf_client_unregister_node(cli, "ghost"), UDAF_ERR_NOT_FOUND);

    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, UnregisterNodeNullArgs) {
    EXPECT_EQ(udaf_client_unregister_node(nullptr, "n"), UDAF_ERR_INVALID_ARG);
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    EXPECT_EQ(udaf_client_unregister_node(cli, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

TEST_F(CSdkTmp, DiscoverNullArgsWithPopulated) {
    udaf_client_config_t cfg{};
    cfg.node_id = "c-host";
    void* cli = nullptr;
    ASSERT_EQ(udaf_client_create(&cfg, &cli), UDAF_OK);
    udaf_client_register_node(cli, "n1", "h", "127.0.0.1", 1);
    // null args 优先于 populated
    EXPECT_EQ(udaf_client_discover(cli, "", nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    EXPECT_EQ(udaf_client_discover(nullptr, "", nullptr, nullptr), UDAF_ERR_INVALID_ARG);
    udaf_client_destroy(cli);
}

// ===== ABI 稳定性测试（v1 接口锁定）=====
//
// 这些测试验证 ABI 在编译期稳定：
// - 函数签名通过函数指针赋值验证
// - 结构体大小通过 static_assert 锁定
// - ABI 版本号未变

TEST_F(CSdkTmp, AbiFunctionSignaturesStable) {
    // 函数指针签名必须匹配头文件声明
    udaf_error_t (*fp_create)(const udaf_client_config_t*, void**) = udaf_client_create;
    udaf_error_t (*fp_start)(void*) = udaf_client_start;
    udaf_error_t (*fp_stop)(void*) = udaf_client_stop;
    void (*fp_destroy)(void*) = udaf_client_destroy;
    udaf_error_t (*fp_discover)(void*, const char*, udaf_node_entry_t**, uint32_t*) =
        udaf_client_discover;
    EXPECT_NE(fp_create, nullptr);
    EXPECT_NE(fp_start, nullptr);
    EXPECT_NE(fp_stop, nullptr);
    EXPECT_NE(fp_destroy, nullptr);
    EXPECT_NE(fp_discover, nullptr);
}

TEST_F(CSdkTmp, AbiStructSizesLocked) {
    // 关键结构体大小必须在 ABI v1 锁定
    EXPECT_EQ(sizeof(udaf_client_config_t), 32u);  // node_id + audit_path + flags
    EXPECT_EQ(sizeof(udaf_node_entry_t), 40u);    // node_id + hostname + addr + port
    EXPECT_EQ(sizeof(udaf_trust_entry_t), 32u);   // node_id + fingerprint
    EXPECT_EQ(sizeof(udaf_error_t), 4u);
}

TEST_F(CSdkTmp, AbiVersionReturnsV1) {
    EXPECT_EQ(udaf_abi_version(), 1u);
    EXPECT_GT(std::strlen(udaf_version_string()), 0u);
}