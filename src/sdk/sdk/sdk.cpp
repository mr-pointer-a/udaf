// sdk.cpp - Client PIMPL 实现
#include "sdk.hpp"

#include "ability_a/registry/service_registry.hpp"
#include "ability_a/trust/peer_whitelist.hpp"
#include "audit/audit.hpp"

#include <sstream>
#include <utility>

namespace udaf::sdk {

struct Client::Impl {
    ClientConfig                                          cfg;
    std::shared_ptr<udaf::ability_a::registry::ServiceRegistry> registry;
    std::shared_ptr<udaf::audit::AuditLogger>             audit_logger;
    std::shared_ptr<udaf::ability_a::trust::PeerWhitelist> whitelist;
};

Client::Client(ClientConfig cfg) {
    auto impl = std::make_unique<Impl>();
    impl->cfg = std::move(cfg);
    impl->registry = std::make_shared<udaf::ability_a::registry::ServiceRegistry>();
    impl->whitelist = std::make_shared<udaf::ability_a::trust::PeerWhitelist>();
    if (!impl->cfg.audit_path.empty()) {
        impl->audit_logger = std::make_shared<udaf::audit::AuditLogger>(impl->cfg.audit_path);
    }
    impl_ = std::move(impl);
}

Client::~Client() = default;

core::Result<void> Client::start() noexcept {
    if (impl_->audit_logger) {
        (void)impl_->audit_logger->append(
            udaf::audit::ActionType::NodeRegister,
            impl_->cfg.node_id, "client", "{\"event\":\"start\"}");
    }
    return core::Result<void>::ok();
}

core::Result<void> Client::stop() noexcept {
    if (impl_->audit_logger) {
        (void)impl_->audit_logger->append(
            udaf::audit::ActionType::NodeUnregister,
            impl_->cfg.node_id, "client", "{\"event\":\"stop\"}");
    }
    return core::Result<void>::ok();
}

std::vector<udaf::ability_a::registry::RegistryEntry>
Client::discover(const std::string& capability_filter) noexcept {
    (void)capability_filter;  // 简化：直接全部返回
    return impl_->registry->snapshot();
}

core::Result<std::uint64_t>
Client::audit(udaf::audit::ActionType action, std::string target,
              std::string params_json) noexcept {
    if (!impl_->audit_logger) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }
    return impl_->audit_logger->append(action, impl_->cfg.node_id,
                                       std::move(target), std::move(params_json));
}

std::uint64_t Client::sequence() const noexcept {
    return impl_->audit_logger ? impl_->audit_logger->sequence() : 0;
}

// ============ 拓扑 / 文件 / 调度（CLI 占位实装） ============

TopologySummary Client::topology_summary() noexcept {
    TopologySummary s;
    auto entries = impl_->registry->snapshot();
    s.node_count = entries.size();
    for (const auto& e : entries) {
        s.nodes.push_back(e.node_id_);
    }
    return s;
}

core::Result<std::uint64_t>
Client::push_file(const std::string& src, const std::string& dst_node,
                  const std::string& dst_path) noexcept {
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    if (src.empty() || dst_node.empty() || dst_path.empty()) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INVALID_ARG);
    }
    // 白名单校验
    if (!impl_->whitelist->contains(dst_node)) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
    }
    std::ostringstream json;
    json << "{\"src\":\"" << src << "\",\"dst_node\":\"" << dst_node
         << "\",\"dst_path\":\"" << dst_path << "\"}";
    return impl_->audit_logger->append(udaf::audit::ActionType::FileTransfer,
                                       impl_->cfg.node_id, dst_node, json.str());
}

core::Result<std::uint64_t>
Client::pull_file(const std::string& src_node, const std::string& src_path,
                  const std::string& dst) noexcept {
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    if (!impl_->whitelist->contains(src_node)) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
    }
    std::ostringstream json;
    json << "{\"src_node\":\"" << src_node << "\",\"src_path\":\""
         << src_path << "\",\"dst\":\"" << dst << "\"}";
    return impl_->audit_logger->append(udaf::audit::ActionType::FileTransfer,
                                       impl_->cfg.node_id, src_node, json.str());
}

core::Result<std::uint64_t>
Client::run_remote(const std::string& node_id, const std::string& command,
                   std::vector<std::string> args) noexcept {
    if (!impl_->whitelist->contains(node_id)) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
    }
    if (command.empty()) return core::Result<std::uint64_t>::err(core::ErrorCode::INVALID_ARG);
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    std::ostringstream json;
    json << "{\"cmd\":\"" << command << "\",\"args\":[";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) json << ",";
        json << "\"" << args[i] << "\"";
    }
    json << "]}";
    return impl_->audit_logger->append(udaf::audit::ActionType::CmdExec,
                                       impl_->cfg.node_id, node_id, json.str());
}

std::vector<std::string> Client::list_nodes() noexcept {
    std::vector<std::string> out;
    for (const auto& e : impl_->registry->snapshot()) out.push_back(e.node_id_);
    return out;
}

core::Result<bool>
Client::register_node(std::string node_id, std::string hostname,
                      std::string bind_addr, std::uint16_t bind_port) noexcept {
    udaf::ability_a::registry::RegistryEntry e;
    e.node_id_      = std::move(node_id);
    e.hostname_     = std::move(hostname);
    e.bind_address_ = std::move(bind_addr);
    e.bind_port_    = bind_port;
    return impl_->registry->register_node(e);
}

core::Result<bool> Client::unregister_node(const std::string& node_id) noexcept {
    return impl_->registry->unregister_node(node_id);
}

std::vector<TrustEntry> Client::trust_list() noexcept {
    std::vector<TrustEntry> out;
    if (!impl_->whitelist) return out;
    auto entries = impl_->whitelist->snapshot();
    out.reserve(entries.size());
    for (const auto& e : entries) {
        TrustEntry t;
        t.node_id = e.node_id;
        // 将 32 字节 fingerprint 转 hex 字符串（C 接口需要）
        t.fingerprint_sha256_hex.reserve(64);
        for (auto b : e.fingerprint_sha256_) {
            char hi = static_cast<char>((b >> 4) & 0x0F);
            char lo = static_cast<char>(b & 0x0F);
            t.fingerprint_sha256_hex.push_back(static_cast<char>(hi < 10 ? '0' + hi : 'a' + hi - 10));
            t.fingerprint_sha256_hex.push_back(static_cast<char>(lo < 10 ? '0' + lo : 'a' + lo - 10));
        }
        t.capabilities.assign(e.allowed_capabilities_.begin(),
                              e.allowed_capabilities_.end());
        out.push_back(std::move(t));
    }
    return out;
}

core::Result<bool>
Client::trust_add(std::string node_id, std::string fingerprint_hex,
                  std::vector<std::string> capabilities) noexcept {
    udaf::ability_a::trust::WhitelistEntry e;
    e.node_id = std::move(node_id);
    for (auto& c : capabilities) e.allowed_capabilities_.insert(std::move(c));
    if (fingerprint_hex.size() != 64) {
        return core::Result<bool>::err(core::ErrorCode::INVALID_ARG);
    }
    e.fingerprint_sha256_.reserve(32);
    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    for (std::size_t i = 0; i < 64; i += 2) {
        int hi = hexv(fingerprint_hex[i]);
        int lo = hexv(fingerprint_hex[i + 1]);
        if (hi < 0 || lo < 0) return core::Result<bool>::err(core::ErrorCode::INVALID_ARG);
        e.fingerprint_sha256_.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    auto r = impl_->whitelist->add(e);
    if (r.is_ok() && impl_->audit_logger) {
        (void)impl_->audit_logger->append(udaf::audit::ActionType::WhitelistUpdate,
                                          impl_->cfg.node_id, e.node_id,
                                          "{\"event\":\"add\"}");
    }
    return r;
}

core::Result<bool> Client::trust_remove(const std::string& node_id) noexcept {
    try {
        auto r = impl_->whitelist->remove(node_id);
        if (r.is_err()) return r;
        if (!r.value()) return core::Result<bool>::err(core::ErrorCode::NODE_NOT_FOUND);
        if (impl_->audit_logger) {
            (void)impl_->audit_logger->append(udaf::audit::ActionType::WhitelistUpdate,
                                              impl_->cfg.node_id, node_id,
                                              "{\"event\":\"remove\"}");
        }
        return r;
    } catch (const std::exception&) {
        return core::Result<bool>::err(core::ErrorCode::INTERNAL);
    }
}

core::Result<std::uint64_t>
Client::psk_rotate(const std::string& new_psk_path) noexcept {
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    if (new_psk_path.empty()) return core::Result<std::uint64_t>::err(core::ErrorCode::INVALID_ARG);
    std::ostringstream json;
    json << "{\"new_psk_path\":\"" << new_psk_path << "\"}";
    return impl_->audit_logger->append(udaf::audit::ActionType::CredentialRotate,
                                       impl_->cfg.node_id, "self", json.str());
}

core::Result<std::uint64_t>
Client::auth_psk(const std::string& node_id) noexcept {
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    if (!impl_->whitelist->contains(node_id)) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
    }
    return impl_->audit_logger->append(udaf::audit::ActionType::PskHandshake,
                                       impl_->cfg.node_id, node_id, "{\"mode\":\"psk\"}");
}

core::Result<std::uint64_t>
Client::migrate(const std::string& src_path, const std::string& dst_path) noexcept {
    if (!impl_->audit_logger) return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    if (src_path.empty() || dst_path.empty()) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INVALID_ARG);
    }
    std::ostringstream json;
    json << "{\"src\":\"" << src_path << "\",\"dst\":\"" << dst_path << "\"}";
    return impl_->audit_logger->append(udaf::audit::ActionType::AuditExport,
                                       impl_->cfg.node_id, "self", json.str());
}

std::string Client::config_show() noexcept {
    std::ostringstream oss;
    oss << "node_id=" << impl_->cfg.node_id
        << " bind_address=" << impl_->cfg.bind_address
        << " bind_port=" << impl_->cfg.bind_port
        << " audit_path=" << impl_->cfg.audit_path
        << " nodes=" << impl_->registry->size()
        << " trust_entries=" << impl_->whitelist->size();
    return oss.str();
}

}  // namespace udaf::sdk
