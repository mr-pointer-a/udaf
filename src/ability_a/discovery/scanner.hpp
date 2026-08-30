// scanner.hpp - 主动扫描者
//
// 设计要点：
//   - 被动接收 broadcast
//   - 重放防护：nonce 去重（窗口 5 分钟）
//   - shared_mutex 多读单写
//   - MAC 校验失败静默丢弃

#ifndef UDAF_ABILITY_A_DISCOVERY_SCANNER_HPP
#define UDAF_ABILITY_A_DISCOVERY_SCANNER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ability_a/registry/service_registry.hpp"
#include "ability_a/transport/udp_socket.hpp"
#include "advertisement.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::discovery {

struct ScannerConfig {
    std::uint16_t bind_port = 19999;
    std::chrono::seconds replay_window{300};  // 5 分钟 nonce 去重窗口
};

class Scanner {
public:
    /// @param registry 注册表（Scanner 收到广告后写入 registry）
    /// @param psk 可选 PSK（32B）用于 MAC 校验
    static std::unique_ptr<Scanner>
    create(udaf::ability_a::registry::ServiceRegistry* registry,
           ScannerConfig cfg,
           std::span<const std::uint8_t> psk = {}) noexcept;

    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;
    Scanner(Scanner&&) = delete;
    Scanner& operator=(Scanner&&) = delete;

    core::Result<void> start() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    /// 接收一条（同步 + 非阻塞）：返回 Ok(true) 有新数据；Err(NET_TIMEOUT) 无数据
    [[nodiscard]] core::Result<bool>
    poll_once() noexcept;

    /// 解析 + 校验 + 注册一条广播数据
    [[nodiscard]] core::Result<bool>
    handle_frame(std::span<const std::uint8_t> frame) noexcept;

    /// 已发现的节点数
    [[nodiscard]] std::size_t seen_count() const noexcept;

    /// 清空 nonce 缓存（仅测试）
    void clear_nonces() noexcept;

private:
    Scanner() = default;

    std::thread thread_;
    std::atomic<bool> running_{false};
    udaf::ability_a::registry::ServiceRegistry* registry_ = nullptr;
    ScannerConfig cfg_;
    std::vector<std::uint8_t> psk_;
    std::unique_ptr<udaf::ability_a::transport::UdpSocket> sock_;

    mutable std::shared_mutex nonces_mtx_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> seen_nonces_;
};

}  // namespace udaf::ability_a::discovery

#endif  // UDAF_ABILITY_A_DISCOVERY_SCANNER_HPP