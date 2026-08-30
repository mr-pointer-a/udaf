// config_loader.cpp - yaml-cpp 后端实现
#include "config/config_loader.hpp"

#include "log/logger.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace udaf::core {

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

}  // namespace

std::optional<NetworkMode> parse_network_mode(std::string_view s) noexcept {
    const auto lower = to_lower(s);
    if (lower == "psk") return NetworkMode::Psk;
    if (lower == "pki") return NetworkMode::Pki;
    return std::nullopt;
}

std::string_view to_string(NetworkMode mode) noexcept {
    switch (mode) {
    case NetworkMode::Psk: return "psk";
    case NetworkMode::Pki: return "pki";
    }
    return "psk";
}

std::optional<LogLevel> parse_log_level(std::string_view s) noexcept {
    const auto lower = to_lower(s);
    if (lower == "trace") return LogLevel::Trace;
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info")  return LogLevel::Info;
    if (lower == "warn" || lower == "warning") return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    if (lower == "critical" || lower == "fatal") return LogLevel::Critical;
    if (lower == "off") return LogLevel::Off;
    return std::nullopt;
}

Result<Config> ConfigLoader::load_from_string(const std::string& yaml_content) const noexcept {
    Config cfg;
    YAML::Node root;
    try {
        root = YAML::Load(yaml_content);
    } catch (const YAML::Exception& e) {
        Logger::instance().log_with_error(
            LogLevel::Error, "yaml parse failed", ErrorCode::CONFIG_PARSE_FAILED);
        return Result<Config>::err(ErrorCode::CONFIG_PARSE_FAILED);
    }

    if (!root) {
        return Result<Config>::err(ErrorCode::CONFIG_PARSE_FAILED);
    }

    try {
        // 顶层字段
        if (root["node_id"])      cfg.node_id = root["node_id"].as<std::string>();
        if (root["node_role"])    cfg.node_role = root["node_role"].as<std::string>();
        if (root["schema_version"]) {
            cfg.schema_version = root["schema_version"].as<std::uint32_t>();
        }

        // net 子表
        if (root["net"]) {
            const auto& n = root["net"];
            if (n["bind_address"])            cfg.net.bind_address = n["bind_address"].as<std::string>();
            if (n["bind_port"])               cfg.net.bind_port = n["bind_port"].as<std::uint16_t>();
            if (n["heartbeat_interval_ms"])   cfg.net.heartbeat_interval_ms = n["heartbeat_interval_ms"].as<std::uint32_t>();
            if (n["discovery_interval_sec"])  cfg.net.discovery_interval_sec = n["discovery_interval_sec"].as<std::uint32_t>();
        }

        // log 子表
        if (root["log"]) {
            const auto& l = root["log"];
            if (l["level"])              cfg.log.level = l["level"].as<std::string>();
            if (l["to_file"])            cfg.log.to_file = l["to_file"].as<bool>();
            if (l["file_path"])          cfg.log.file_path = l["file_path"].as<std::string>();
            if (l["max_file_size"])      cfg.log.max_file_size_bytes = l["max_file_size"].as<std::uint64_t>();
            if (l["max_rotated_files"])  cfg.log.max_rotated_files = l["max_rotated_files"].as<std::uint32_t>();
        }

        // crypto 子表
        if (root["crypto"]) {
            const auto& c = root["crypto"];
            if (c["mode"]) {
                auto mode = parse_network_mode(c["mode"].as<std::string>());
                if (!mode) {
                    Logger::instance().log_with_error(
                        LogLevel::Error, "crypto.mode invalid", ErrorCode::CONFIG_INVALID_VALUE);
                    return Result<Config>::err(ErrorCode::CONFIG_INVALID_VALUE);
                }
                cfg.crypto.mode = *mode;
            }
            if (c["psk_path"])   cfg.crypto.psk_path = c["psk_path"].as<std::string>();
            if (c["cert_path"])  cfg.crypto.cert_path = c["cert_path"].as<std::string>();
            if (c["key_path"])   cfg.crypto.key_path = c["key_path"].as<std::string>();
            if (c["ca_path"])    cfg.crypto.ca_path = c["ca_path"].as<std::string>();
        }

        // peer_whitelist（数组）
        if (root["peer_whitelist"] && root["peer_whitelist"].IsSequence()) {
            for (const auto& item : root["peer_whitelist"]) {
                cfg.peer_whitelist.push_back(item.as<std::string>());
            }
        }
    } catch (const YAML::Exception& e) {
        Logger::instance().log_with_error(
            LogLevel::Error, "yaml field extract failed", ErrorCode::CONFIG_INVALID_VALUE);
        return Result<Config>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }

    // 完整性校验
    auto vr = validate(cfg);
    if (vr.is_err()) {
        return Result<Config>::err(vr.error());
    }
    return Result<Config>::ok(std::move(cfg));
}

Result<Config> ConfigLoader::load_from_file(const std::string& file_path) const noexcept {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        Logger::instance().log_with_error(
            LogLevel::Error, "config file open failed", ErrorCode::CONFIG_PARSE_FAILED);
        return Result<Config>::err(ErrorCode::CONFIG_PARSE_FAILED);
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return load_from_string(ss.str());
}

Result<void> ConfigLoader::validate(const Config& cfg) const noexcept {
    if (cfg.node_id.empty()) {
        return Result<void>::err(ErrorCode::CONFIG_MISSING_REQUIRED);
    }
    if (cfg.node_role != "host" && cfg.node_role != "device") {
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }
    if (cfg.net.bind_port == 0) {
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }
    if (cfg.net.heartbeat_interval_ms == 0) {
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }
    if (cfg.net.discovery_interval_sec == 0) {
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }
    if (!parse_log_level(cfg.log.level)) {
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }
    if (cfg.node_role == "device" && cfg.crypto.mode == NetworkMode::Pki) {
        // 设备端暂不支持 PKI（仅 host 端支持完整 CA 链校验）
        // 当前仅约束软警告，不强制拒绝
    }
    if (cfg.crypto.mode == NetworkMode::Psk && cfg.crypto.psk_path.empty()) {
        return Result<void>::err(ErrorCode::CONFIG_MISSING_REQUIRED);
    }
    if (cfg.crypto.mode == NetworkMode::Pki) {
        if (cfg.crypto.cert_path.empty() || cfg.crypto.key_path.empty() ||
            cfg.crypto.ca_path.empty()) {
            return Result<void>::err(ErrorCode::CONFIG_MISSING_REQUIRED);
        }
    }
    return Result<void>::ok();
}

}  // namespace udaf::core