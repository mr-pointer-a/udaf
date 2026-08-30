// service_registry.hpp - ServiceRegistry（多读单写 + 订阅）
//
// 设计要点：
//   - 共享互斥 shared_mutex 多读单写（性能契约 #14：10000 条查询 < 100ms）
//   - SubscriptionHandle RAII（析构自动 unsubscribe）
//   - 节点去重（同 node_id_ 后注册覆盖前者，订阅者收到 Update）
//   - 订阅通过 std::function 注入（不持有 ability_b 任何类型）

#ifndef UDAF_ABILITY_A_REGISTRY_SERVICE_REGISTRY_HPP
#define UDAF_ABILITY_A_REGISTRY_SERVICE_REGISTRY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::registry {

/// 单个服务描述
struct ServiceDescriptor {
    std::string service_name;
    std::uint16_t port = 0;
    std::string protocol;   // "tcp" / "udp" / "zmq" ...
};

/// 注册表条目（节点）
struct RegistryEntry {
    std::string node_id_;
    std::string hostname_;
    std::string bind_address_;
    std::uint16_t bind_port_ = 0;
    std::vector<ServiceDescriptor> services_;
    std::int64_t last_heartbeat_ns_ = 0;
};

/// 订阅事件
enum class RegistryEvent : std::uint8_t { Add, Remove, Update };

/// 订阅者回调签名
using RegistryCallback =
    std::function<void(RegistryEvent, const RegistryEntry&)>;

/// 订阅句柄（RAII）：析构自动取消订阅
class SubscriptionHandle {
public:
    SubscriptionHandle() = default;
    explicit SubscriptionHandle(class ServiceRegistry* owner, std::uint64_t id) noexcept
        : owner_(owner), id_(id) {}
    ~SubscriptionHandle();
    SubscriptionHandle(SubscriptionHandle&& other) noexcept;
    SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept;
    SubscriptionHandle(const SubscriptionHandle&) = delete;
    SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;

    void release() noexcept;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] bool valid() const noexcept { return owner_ != nullptr; }

private:
    class ServiceRegistry* owner_ = nullptr;
    std::uint64_t id_ = 0;
};

/// 服务注册表（多读单写 + 订阅）
class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry();

    /// 注册/更新节点（同 node_id_ 覆盖）
    /// @return Ok(true) 新增；Ok(false) 更新
    [[nodiscard]] core::Result<bool>
    register_node(const RegistryEntry& entry) noexcept;

    /// 移除节点
    [[nodiscard]] core::Result<bool>
    unregister_node(std::string_view node_id) noexcept;

    /// 查询节点
    [[nodiscard]] core::Result<RegistryEntry>
    get_node(std::string_view node_id) const noexcept;

    /// 查询所有节点（拷贝）
    [[nodiscard]] std::vector<RegistryEntry> snapshot() const noexcept;

    /// 节点数
    [[nodiscard]] std::size_t size() const noexcept;

    /// 清空（仅用于测试）
    void clear() noexcept;

    /// 订阅变更
    [[nodiscard]] std::unique_ptr<SubscriptionHandle>
    subscribe(RegistryCallback cb) noexcept;

    /// 取消订阅（订阅句柄析构时自动调用）
    void unsubscribe(std::uint64_t handle_id) noexcept;

private:
    void notify(RegistryEvent ev, const RegistryEntry& entry) noexcept;

    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, RegistryEntry> nodes_;

    std::mutex sub_mtx_;
    std::unordered_map<std::uint64_t, RegistryCallback> subs_;
    std::uint64_t next_sub_id_ = 1;
};

}  // namespace udaf::ability_a::registry

#endif  // UDAF_ABILITY_A_REGISTRY_SERVICE_REGISTRY_HPP