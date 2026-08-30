// sdk.hpp - 阶段 E3
//
// 设计要点（03 §9.4~§9.6）：
//   - Client::Impl 持有 7 个 shared_ptr（registry/topology/scheduler/wal/whitelist/coordinator/audit）
//   - 同步原语 + 回调表 + 析构顺序约束
//   - C 接口 13+11 函数 + 4+4 结构体（设备端 + 主机端）

#ifndef UDAF_SDK_SDK_HPP
#define UDAF_SDK_SDK_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ability_a/registry/service_registry.hpp"
#include "ability_a/trust/peer_whitelist.hpp"
#include "audit/audit.hpp"
#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::sdk {

struct ClientConfig {
    std::string node_id;
    std::string bind_address;
    std::uint16_t bind_port = 0;
    std::string audit_path;
};

struct TopologySummary {
    std::size_t node_count = 0;
    std::size_t edge_count = 0;
    std::vector<std::string> nodes;
};

struct TrustEntry {
    std::string node_id;
    std::string fingerprint_sha256_hex;
    std::vector<std::string> capabilities;
};

struct SubscriptionHandle {
    std::uint64_t id = 0;
    void* owner = nullptr;  // 指向 Client
};

class Client {
public:
    explicit Client(ClientConfig cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// 启动 / 停止
    core::Result<void> start() noexcept;
    core::Result<void> stop() noexcept;

    /// 查询当前注册节点（按 capability 过滤；空 = 全部）
    std::vector<udaf::ability_a::registry::RegistryEntry>
    discover(std::string capability_filter) noexcept;

    /// 注册一条审计事件（便捷 API）
    [[nodiscard]] core::Result<std::uint64_t>
    audit(udaf::audit::ActionType action, std::string target,
          std::string params_json) noexcept;

    /// 当前 sequence
    [[nodiscard]] std::uint64_t sequence() const noexcept;

    /// ----- 拓扑 / 文件 / 调度（CLI 占位实装） -----

    /// 拓扑摘要（节点 + 边计数 + 节点列表）
    [[nodiscard]] TopologySummary topology_summary() noexcept;

    /// 推 / 拉文件（占位：返回审计 sequence 表示"任务已创建"）
    [[nodiscard]] core::Result<std::uint64_t>
    push_file(std::string src, std::string dst_node, std::string dst_path) noexcept;
    [[nodiscard]] core::Result<std::uint64_t>
    pull_file(std::string src_node, std::string src_path, std::string dst) noexcept;

    /// 在远端执行命令（占位：白名单 /bin/echo）
    [[nodiscard]] core::Result<std::uint64_t>
    run_remote(std::string node_id, std::string command, std::vector<std::string> args) noexcept;

    /// 节点管理（占位：基于 registry snapshot）
    [[nodiscard]] std::vector<std::string> list_nodes() noexcept;
    [[nodiscard]] core::Result<bool> register_node(std::string node_id,
                                                    std::string hostname,
                                                    std::string bind_addr,
                                                    std::uint16_t bind_port) noexcept;
    [[nodiscard]] core::Result<bool> unregister_node(std::string node_id) noexcept;

    /// 白名单管理
    [[nodiscard]] std::vector<TrustEntry> trust_list() noexcept;
    [[nodiscard]] core::Result<bool>
    trust_add(std::string node_id, std::string fingerprint_hex,
              std::vector<std::string> capabilities) noexcept;
    [[nodiscard]] core::Result<bool>
    trust_remove(std::string node_id) noexcept;

    /// PSK 轮转 / 认证（占位）
    [[nodiscard]] core::Result<std::uint64_t>
    psk_rotate(std::string new_psk_path) noexcept;
    [[nodiscard]] core::Result<std::uint64_t>
    auth_psk(std::string node_id) noexcept;

    /// 迁移（占位）
    [[nodiscard]] core::Result<std::uint64_t>
    migrate(std::string src_path, std::string dst_path) noexcept;

    /// 配置：show（仅打印 cfg + 注册表大小）
    [[nodiscard]] std::string config_show() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace udaf::sdk

#endif  // UDAF_SDK_SDK_HPP