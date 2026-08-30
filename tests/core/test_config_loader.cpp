// test_config_loader.cpp - ConfigLoader 加载与校验测试（共 6 用例）
#include "config/config_loader.hpp"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>

using udaf::core::ConfigLoader;
using udaf::core::Result;
using udaf::core::ErrorCode;
using udaf::core::NetworkMode;
using udaf::core::parse_network_mode;
using udaf::core::to_string;
using udaf::core::parse_log_level;

TEST(ConfigLoader, LoadValidYaml) {
    const std::string yaml = R"YAML(
node_id: "host-001"
node_role: "host"
schema_version: 1
net:
  bind_address: "127.0.0.1"
  bind_port: 9999
  heartbeat_interval_ms: 1000
  discovery_interval_sec: 30
log:
  level: "debug"
crypto:
  mode: "psk"
  psk_path: "/etc/udaf/psk.bin"
peer_whitelist:
  - "fp_aabbcc"
  - "fp_ddeeff"
)YAML";

    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_ok()) << "valid yaml should parse";
    const auto& cfg = r.value();
    EXPECT_EQ(cfg.node_id, "host-001");
    EXPECT_EQ(cfg.node_role, "host");
    EXPECT_EQ(cfg.schema_version, 1u);
    EXPECT_EQ(cfg.net.bind_address, "127.0.0.1");
    EXPECT_EQ(cfg.net.bind_port, 9999);
    EXPECT_EQ(cfg.log.level, "debug");
    EXPECT_EQ(cfg.crypto.mode, NetworkMode::Psk);
    EXPECT_EQ(cfg.crypto.psk_path, "/etc/udaf/psk.bin");
    ASSERT_EQ(cfg.peer_whitelist.size(), 2u);
    EXPECT_EQ(cfg.peer_whitelist[0], "fp_aabbcc");
}

TEST(ConfigLoader, LoadMissingRequiredReturnsErr) {
    // 缺 node_id
    const std::string yaml = R"YAML(
node_role: "host"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_MISSING_REQUIRED);
}

TEST(ConfigLoader, LoadInvalidRoleReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "unknown"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, LoadPkiMissingCertReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
crypto:
  mode: "pki"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_MISSING_REQUIRED);
}

TEST(ConfigLoader, LoadFromFile) {
    const std::string path = "/tmp/udaf_test_config.yaml";
    {
        std::ofstream ofs(path);
        ofs << R"YAML(
node_id: "host-file"
node_role: "device"
net:
  bind_address: "0.0.0.0"
  bind_port: 7777
log:
  level: "info"
crypto:
  mode: "psk"
  psk_path: "/tmp/psk.bin"
)YAML";
    }
    ConfigLoader loader;
    auto r = loader.load_from_file(path);
    ASSERT_TRUE(r.is_ok()) << "file load should succeed";
    EXPECT_EQ(r.value().node_id, "host-file");
    EXPECT_EQ(r.value().net.bind_port, 7777);
    std::remove(path.c_str());
}

TEST(ConfigLoader, HelpersRoundTrip) {
    EXPECT_EQ(parse_network_mode("psk"), NetworkMode::Psk);
    EXPECT_EQ(parse_network_mode("PKI"), NetworkMode::Pki);
    EXPECT_FALSE(parse_network_mode("xxx").has_value());
    EXPECT_EQ(to_string(NetworkMode::Psk), "psk");

    ASSERT_TRUE(parse_log_level("DEBUG").has_value());
    EXPECT_EQ(*parse_log_level("warn"), udaf::core::LogLevel::Warn);
    EXPECT_EQ(*parse_log_level("INFO"), udaf::core::LogLevel::Info);
    EXPECT_FALSE(parse_log_level("invalid").has_value());
}

// ===== 边界用例：补齐 validate() 各分支 =====

TEST(ConfigLoader, InvalidBindPortReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
net:
  bind_port: 0
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, ZeroHeartbeatReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
net:
  bind_port: 9000
  heartbeat_interval_ms: 0
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, ZeroDiscoveryIntervalReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
net:
  bind_port: 9000
  heartbeat_interval_ms: 1000
  discovery_interval_sec: 0
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, InvalidLogLevelReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
net:
  bind_port: 9000
  heartbeat_interval_ms: 1000
  discovery_interval_sec: 30
log:
  level: "BOGUS"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, InvalidCryptoModeReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
crypto:
  mode: "xx-unknown"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_INVALID_VALUE);
}

TEST(ConfigLoader, PkiMissingCaReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "h1"
node_role: "host"
crypto:
  mode: "pki"
  cert_path: "/tmp/c.pem"
  key_path: "/tmp/k.pem"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_MISSING_REQUIRED);
}

TEST(ConfigLoader, PskMissingPskPathReturnsErr) {
    const std::string yaml = R"YAML(
node_id: "d1"
node_role: "device"
crypto:
  mode: "psk"
)YAML";
    ConfigLoader loader;
    auto r = loader.load_from_string(yaml);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_MISSING_REQUIRED);
}

TEST(ConfigLoader, LoadFromMissingFileReturnsErr) {
    ConfigLoader loader;
    auto r = loader.load_from_file("/tmp/udaf_nonexistent_xyz_12345.yaml");
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_PARSE_FAILED);
}

TEST(ConfigLoader, ParseLogLevelAliases) {
    // warning ↔ warn、fatal ↔ critical 大小写不敏感
    EXPECT_EQ(*parse_log_level("warning"), udaf::core::LogLevel::Warn);
    EXPECT_EQ(*parse_log_level("fatal"),   udaf::core::LogLevel::Critical);
    EXPECT_EQ(*parse_log_level("trace"),   udaf::core::LogLevel::Trace);
    EXPECT_EQ(*parse_log_level("off"),     udaf::core::LogLevel::Off);
}

TEST(ConfigLoader, NetworkModeUnknownReturnsNullopt) {
    EXPECT_FALSE(parse_network_mode("").has_value());
    EXPECT_FALSE(parse_network_mode("psk ").has_value());
}