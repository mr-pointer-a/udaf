// peer_whitelist.cpp - PeerWhitelist 实现
#include "peer_whitelist.hpp"

#include <utility>

namespace udaf::ability_a::trust {

core::Result<bool>
PeerWhitelist::add(const WhitelistEntry& entry) noexcept {
    if (entry.node_id.empty() || entry.fingerprint_sha256_.size() != 32) {
        return core::Result<bool>::err(core::ErrorCode::INVALID_ARG);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    bool is_new = entries_.find(entry.node_id) == entries_.end();
    entries_[entry.node_id] = entry;
    return core::Result<bool>::ok(is_new);
}

core::Result<bool>
PeerWhitelist::remove(std::string_view node_id) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    bool removed = entries_.erase(std::string(node_id)) > 0;
    return core::Result<bool>::ok(removed);
}

bool
PeerWhitelist::contains(std::string_view node_id,
                        std::string_view capability) const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = entries_.find(std::string(node_id));
    if (it == entries_.end()) return false;
    if (capability.empty()) return true;
    return it->second.allowed_capabilities_.count(std::string(capability)) > 0;
}

core::Result<WhitelistEntry>
PeerWhitelist::get(std::string_view node_id) const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = entries_.find(std::string(node_id));
    if (it == entries_.end()) {
        return core::Result<WhitelistEntry>::err(core::ErrorCode::BIZ_NODE_NOT_REGISTERED);
    }
    return core::Result<WhitelistEntry>::ok(it->second);
}

std::size_t PeerWhitelist::size() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return entries_.size();
}

std::vector<WhitelistEntry> PeerWhitelist::snapshot() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<WhitelistEntry> out;
    out.reserve(entries_.size());
    for (const auto& [k, v] : entries_) {
        out.push_back(v);
    }
    return out;
}

void PeerWhitelist::clear() noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    entries_.clear();
}

}  // namespace udaf::ability_a::trust