// service_registry.cpp - ServiceRegistry 实现
#include "service_registry.hpp"

#include <chrono>
#include <utility>

namespace udaf::ability_a::registry {

// ---------- SubscriptionHandle ----------

SubscriptionHandle::SubscriptionHandle(SubscriptionHandle&& other) noexcept
    : owner_(other.owner_), id_(other.id_) {
    other.owner_ = nullptr;
    other.id_ = 0;
}

SubscriptionHandle& SubscriptionHandle::operator=(SubscriptionHandle&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        id_ = other.id_;
        other.owner_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

SubscriptionHandle::~SubscriptionHandle() { release(); }

void SubscriptionHandle::release() noexcept {
    if (owner_ != nullptr) {
        owner_->unsubscribe(id_);
        owner_ = nullptr;
        id_ = 0;
    }
}

// ---------- ServiceRegistry ----------

ServiceRegistry::~ServiceRegistry() = default;

core::Result<bool>
ServiceRegistry::register_node(const RegistryEntry& entry) noexcept {
    if (entry.node_id_.empty()) {
        return core::Result<bool>::err(core::ErrorCode::INVALID_ARG);
    }
    bool is_new = false;
    {
        std::unique_lock lock(mtx_);
        auto it = nodes_.find(entry.node_id_);
        if (it == nodes_.end()) {
            is_new = true;
            nodes_.emplace(entry.node_id_, entry);
        } else {
            it->second = entry;
        }
    }
    notify(is_new ? RegistryEvent::Add : RegistryEvent::Update, entry);
    return core::Result<bool>::ok(is_new);
}

core::Result<bool>
ServiceRegistry::unregister_node(std::string_view node_id) noexcept {
    RegistryEntry removed;
    bool ok = false;
    {
        std::unique_lock lock(mtx_);
        auto it = nodes_.find(std::string(node_id));
        if (it != nodes_.end()) {
            removed = it->second;
            nodes_.erase(it);
            ok = true;
        }
    }
    if (ok) notify(RegistryEvent::Remove, removed);
    return core::Result<bool>::ok(ok);
}

core::Result<RegistryEntry>
ServiceRegistry::get_node(std::string_view node_id) const noexcept {
    std::shared_lock lock(mtx_);
    auto it = nodes_.find(std::string(node_id));
    if (it == nodes_.end()) {
        return core::Result<RegistryEntry>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
    }
    return core::Result<RegistryEntry>::ok(it->second);
}

std::vector<RegistryEntry> ServiceRegistry::snapshot() const noexcept {
    std::vector<RegistryEntry> out;
    std::shared_lock lock(mtx_);
    out.reserve(nodes_.size());
    for (auto& [k, v] : nodes_) {
        (void)k;
        out.push_back(v);
    }
    return out;
}

std::size_t ServiceRegistry::size() const noexcept {
    std::shared_lock lock(mtx_);
    return nodes_.size();
}

void ServiceRegistry::clear() noexcept {
    std::unique_lock lock(mtx_);
    nodes_.clear();
}

std::unique_ptr<SubscriptionHandle>
ServiceRegistry::subscribe(RegistryCallback cb) noexcept {
    std::lock_guard<std::mutex> lk(sub_mtx_);
    std::uint64_t id = next_sub_id_++;
    subs_[id] = std::move(cb);
    return std::make_unique<SubscriptionHandle>(this, id);
}

void ServiceRegistry::unsubscribe(std::uint64_t handle_id) noexcept {
    std::lock_guard<std::mutex> lk(sub_mtx_);
    subs_.erase(handle_id);
}

void ServiceRegistry::notify(RegistryEvent ev, const RegistryEntry& entry) noexcept {
    std::vector<RegistryCallback> cbs;
    {
        std::lock_guard<std::mutex> lk(sub_mtx_);
        cbs.reserve(subs_.size());
        for (auto& [id, cb] : subs_) {
            (void)id;
            cbs.push_back(cb);
        }
    }
    for (auto& cb : cbs) {
        if (cb) cb(ev, entry);
    }
}

}  // namespace udaf::ability_a::registry