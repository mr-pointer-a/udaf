// udaf_c.cpp - C 接口实现（PIMPL 包装 sdk::Client）
#include "udaf_c.h"

#include "sdk/sdk.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ClientRep {
    std::unique_ptr<udaf::sdk::Client> client;
    std::string node_id_buf;
    std::vector<std::string>            str_buf;       // 给 discover/trust 列表分配字符串
    std::vector<udaf_node_entry_t>      node_entries;
    std::vector<udaf_trust_entry_t>     trust_entries;
};

std::string copy_cstr(const char* s) {
    return s ? std::string(s) : std::string();
}

}  // namespace

extern "C" {

udaf_error_t udaf_client_create(const udaf_client_config_t* cfg, void** out_client) {
    if (!cfg || !out_client) return UDAF_ERR_INVALID_ARG;
    auto rep = std::make_unique<ClientRep>();
    udaf::sdk::ClientConfig c;
    c.node_id      = copy_cstr(cfg->node_id);
    rep->node_id_buf = c.node_id;
    c.bind_address = copy_cstr(cfg->bind_address);
    c.bind_port    = cfg->bind_port;
    c.audit_path   = copy_cstr(cfg->audit_path);
    rep->client = std::make_unique<udaf::sdk::Client>(std::move(c));
    if (rep->client->start().is_err()) return UDAF_ERR_INTERNAL;
    *out_client = rep.release();
    return UDAF_OK;
}

udaf_error_t udaf_client_start(void* client) {
    if (!client) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    return rep->client->start().is_ok() ? UDAF_OK : UDAF_ERR_INTERNAL;
}

udaf_error_t udaf_client_stop(void* client) {
    if (!client) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    return rep->client->stop().is_ok() ? UDAF_OK : UDAF_ERR_INTERNAL;
}

void udaf_client_destroy(void* client) {
    delete static_cast<ClientRep*>(client);
}

udaf_error_t udaf_client_discover(void* client, const char* capability_filter,
                                   udaf_node_entry_t** out_entries, uint32_t* out_count) {
    if (!client || !out_entries || !out_count) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    rep->node_entries.clear();
    rep->str_buf.clear();
    auto entries = rep->client->discover(copy_cstr(capability_filter));
    rep->node_entries.reserve(entries.size());
    // 预 reserve 避免 push_back 期间 reallocate 导致先前 .c_str() 失效
    rep->str_buf.reserve(entries.size() * 3);
    for (const auto& e : entries) {
        rep->str_buf.push_back(e.node_id_);
        rep->str_buf.push_back(e.hostname_);
        rep->str_buf.push_back(e.bind_address_);
        udaf_node_entry_t ne{};
        ne.node_id      = rep->str_buf[rep->str_buf.size() - 3].c_str();
        ne.hostname     = rep->str_buf[rep->str_buf.size() - 2].c_str();
        ne.bind_address = rep->str_buf[rep->str_buf.size() - 1].c_str();
        ne.bind_port    = e.bind_port_;
        ne.service_count = static_cast<uint32_t>(e.services_.size());
        ne.service_names = nullptr;  // 简化
        rep->node_entries.push_back(ne);
    }
    *out_entries = rep->node_entries.data();
    *out_count   = static_cast<uint32_t>(rep->node_entries.size());
    return UDAF_OK;
}

void udaf_client_free_entries(udaf_node_entry_t*, uint32_t) { /* 静态缓冲，无需释放 */ }

udaf_error_t udaf_client_register_node(void* client,
                                        const char* node_id,
                                        const char* hostname,
                                        const char* bind_address,
                                        uint16_t    bind_port) {
    if (!client || !node_id || !hostname || !bind_address) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    auto r = rep->client->register_node(copy_cstr(node_id), copy_cstr(hostname),
                                         copy_cstr(bind_address), bind_port);
    return r.is_ok() ? UDAF_OK : UDAF_ERR_INTERNAL;
}

udaf_error_t udaf_client_unregister_node(void* client, const char* node_id) {
    if (!client || !node_id) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    auto r = rep->client->unregister_node(copy_cstr(node_id));
    if (r.is_err()) return UDAF_ERR_NOT_FOUND;
    return r.value() ? UDAF_OK : UDAF_ERR_NOT_FOUND;
}

uint32_t udaf_client_topology_node_count(void* client) {
    if (!client) return 0;
    auto* rep = static_cast<ClientRep*>(client);
    return static_cast<uint32_t>(rep->client->topology_summary().node_count);
}

udaf_error_t udaf_client_trust_add(void* client, const char* node_id,
                                    const char* fingerprint_hex,
                                    const char* const* capabilities, uint32_t capability_count) {
    if (!client || !node_id || !fingerprint_hex) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    std::vector<std::string> caps;
    for (uint32_t i = 0; i < capability_count; ++i) {
        caps.emplace_back(capabilities[i] ? capabilities[i] : "");
    }
    auto r = rep->client->trust_add(copy_cstr(node_id),
                                     copy_cstr(fingerprint_hex), caps);
    if (r.is_err()) {
        if (r.error() == udaf::core::ErrorCode::INVALID_ARG) return UDAF_ERR_WHITELIST;
        return UDAF_ERR_INTERNAL;
    }
    return UDAF_OK;
}

udaf_error_t udaf_client_trust_remove(void* client, const char* node_id) {
    if (!client || !node_id) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    auto r = rep->client->trust_remove(copy_cstr(node_id));
    return r.is_ok() ? UDAF_OK : UDAF_ERR_NOT_FOUND;
}

udaf_error_t udaf_client_trust_list(void* client, udaf_trust_entry_t** out_entries,
                                     uint32_t* out_count) {
    if (!client || !out_entries || !out_count) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    rep->trust_entries.clear();
    auto list = rep->client->trust_list();
    rep->trust_entries.reserve(list.size());
    for (const auto& t : list) {
        udaf_trust_entry_t te{};
        te.node_id            = t.node_id.c_str();
        te.fingerprint_hex    = t.fingerprint_sha256_hex.c_str();
        te.capability_count   = static_cast<uint32_t>(t.capabilities.size());
        te.capabilities       = t.capabilities.empty()
                                  ? nullptr
                                  : reinterpret_cast<const char* const*>(t.capabilities.data());
        rep->trust_entries.push_back(te);
    }
    *out_entries = rep->trust_entries.empty() ? nullptr : rep->trust_entries.data();
    *out_count   = static_cast<uint32_t>(rep->trust_entries.size());
    return UDAF_OK;
}

void udaf_client_free_trust_entries(udaf_trust_entry_t*, uint32_t) { /* 内部缓冲 */ }

udaf_error_t udaf_client_run_remote(void* client, const char* node_id,
                                      const char* command, const char* const* args,
                                      uint32_t arg_count, udaf_audit_result_t* out) {
    if (!client || !node_id || !command || !out) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    std::vector<std::string> sargs;
    for (uint32_t i = 0; i < arg_count; ++i) {
        sargs.emplace_back(args[i] ? args[i] : "");
    }
    auto r = rep->client->run_remote(copy_cstr(node_id), copy_cstr(command), sargs);
    if (r.is_err()) {
        if (r.error() == udaf::core::ErrorCode::BIZ_AUTH_UNTRUSTED) return UDAF_ERR_WHITELIST;
        if (r.error() == udaf::core::ErrorCode::INVALID_ARG) return UDAF_ERR_INVALID_ARG;
        if (r.error() == udaf::core::ErrorCode::NODE_NOT_FOUND) return UDAF_ERR_NOT_FOUND;
        return UDAF_ERR_INTERNAL;
    }
    out->sequence = r.value();
    out->json_payload = "{}";
    return UDAF_OK;
}

udaf_error_t udaf_client_push_file(void* client, const char* src_path,
                                    const char* dst_node, const char* dst_path,
                                    udaf_audit_result_t* out) {
    if (!client || !src_path || !dst_node || !dst_path || !out) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    auto r = rep->client->push_file(copy_cstr(src_path), copy_cstr(dst_node),
                                     copy_cstr(dst_path));
    if (r.is_err()) {
        if (r.error() == udaf::core::ErrorCode::BIZ_AUTH_UNTRUSTED) return UDAF_ERR_WHITELIST;
        return UDAF_ERR_INVALID_ARG;
    }
    out->sequence = r.value();
    out->json_payload = "{}";
    return UDAF_OK;
}

udaf_error_t udaf_client_pull_file(void* client, const char* src_node,
                                    const char* src_path, const char* dst_path,
                                    udaf_audit_result_t* out) {
    if (!client || !src_node || !src_path || !dst_path || !out) return UDAF_ERR_INVALID_ARG;
    auto* rep = static_cast<ClientRep*>(client);
    auto r = rep->client->pull_file(copy_cstr(src_node), copy_cstr(src_path),
                                     copy_cstr(dst_path));
    if (r.is_err()) {
        if (r.error() == udaf::core::ErrorCode::BIZ_AUTH_UNTRUSTED) return UDAF_ERR_WHITELIST;
        return UDAF_ERR_INVALID_ARG;
    }
    out->sequence = r.value();
    out->json_payload = "{}";
    return UDAF_OK;
}

const char* udaf_version_string(void) {
    return "udaf v0.1.0";
}

uint32_t udaf_abi_version(void) {
    return 1;
}

}  // extern "C"