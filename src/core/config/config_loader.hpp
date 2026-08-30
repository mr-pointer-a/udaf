// config_loader.hpp - UDAF 配置加载器（yaml-cpp 后端）
//
// 设计要点（04 §2.1 + ADR-009）：
//   - 加载 YAML 文件路径 → Config（节点配置 / 网络配置 / 日志配置 / 加密配置）
//   - 必需键缺失 → Result<Config>::err(CONFIG_MISSING_REQUIRED)
//   - 值非法 → Result<Config>::err(CONFIG_INVALID_VALUE)
//   - 文件不存在 / 解析失败 → Result<Config>::err(CONFIG_PARSE_FAILED)
//   - 不抛异常（CLAUDE.md §3.5）
//   - 不 include yaml-cpp（仅 PIMPL 在 .cpp 中持有）

#ifndef UDAF_CORE_CONFIG_CONFIG_LOADER_HPP
#define UDAF_CORE_CONFIG_CONFIG_LOADER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "log/logger.hpp"

#include "error_code.hpp"
#include "result.hpp"

namespace udaf::core {

/// 网络模式（详见 04 §2.10 + ADR-004）
enum class NetworkMode : std::uint8_t {
    Psk = 0,    // 预共享密钥（出厂配置）
    Pki = 1,    // PKI 完整握手（生产环境）
};

/// 日志配置（与 logger.hpp 中的 LoggerConfig 对齐）
struct LogConfig {
    std::string level = "info";
    bool to_file = false;
    std::string file_path;
    std::uint64_t max_file_size_bytes = 0;
    std::uint32_t max_rotated_files = 0;
};

/// 网络配置
struct NetConfig {
    std::string bind_address = "0.0.0.0";
    std::uint16_t bind_port = 9876;
    std::uint32_t heartbeat_interval_ms = 1000;
    std::uint32_t discovery_interval_sec = 30;
};

/// 加密配置
struct CryptoConfig {
    NetworkMode mode = NetworkMode::Psk;
    std::string psk_path;       // PSK 模式：HMAC 密钥文件路径
    std::string cert_path;      // PKI 模式：服务端证书
    std::string key_path;       // PKI 模式：服务端私钥
    std::string ca_path;        // PKI 模式：CA 证书
};

/// UDAF 总配置（包含所有子模块配置）
struct Config {
    std::string node_id;
    std::string node_role;       // "host" / "device"
    NetConfig net;
    LogConfig log;
    CryptoConfig crypto;

    /// 拓扑/发现的 peer 白名单（仅 device 模式生效，参考 04 §2.9）
    /// 列表内为可信 host 的 fingerprint_sha256 字符串。
    std::vector<std::string> peer_whitelist;

    /// 配置文件的 schema_version（用于未来兼容性检查）
    std::uint32_t schema_version = 0;
};

/// ConfigLoader 门面。提供 2 个加载入口 + 1 个校验入口。
class ConfigLoader {
public:
    ConfigLoader() = default;
    ~ConfigLoader() = default;

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

    /// 从 YAML 文件加载。
    /// @param file_path 配置文件绝对路径
    /// @return Ok(Config) / Err(CONFIG_PARSE_FAILED / CONFIG_INVALID_VALUE / ...)
    [[nodiscard]] Result<Config> load_from_file(const std::string& file_path) const noexcept;

    /// 从 YAML 字符串加载（用于单元测试 + 内嵌配置）。
    [[nodiscard]] Result<Config> load_from_string(const std::string& yaml_content) const noexcept;

    /// 校验 Config 完整性与合法性（必需字段、非空约束、值范围等）。
    /// @return Ok() / Err(CONFIG_MISSING_REQUIRED / CONFIG_INVALID_VALUE)
    [[nodiscard]] Result<void> validate(const Config& cfg) const noexcept;
};

/// 将字符串转换为 NetworkMode（不区分大小写）。
[[nodiscard]] std::optional<NetworkMode> parse_network_mode(std::string_view s) noexcept;

/// 将 NetworkMode 转换为字符串。
[[nodiscard]] std::string_view to_string(NetworkMode mode) noexcept;

/// 将字符串转换为 LogLevel（不区分大小写）。
[[nodiscard]] std::optional<LogLevel> parse_log_level(std::string_view s) noexcept;

}  // namespace udaf::core

#endif  // UDAF_CORE_CONFIG_CONFIG_LOADER_HPP