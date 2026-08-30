// audit.hpp - 阶段 E1
//
// 设计要点：
//   - hash chain (SHA-512 prev_hash + params_hash) — ADR-006 §2.1
//   - 19 项 ActionType（v1.7 扩展 6 项）
//   - 创世 hash = SHA-512(NodeId || boot_random || boot_time)
//   - 默认路径 0750+0640
//   - 异步写入（性能契约 #27 ≥ 1000 条/秒）

#ifndef UDAF_AUDIT_AUDIT_HPP
#define UDAF_AUDIT_AUDIT_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::audit {

// 19 项 ActionType（ADR-006 §2.1 + v1.7 扩展 6 项）
enum class ActionType : std::uint16_t {
    NodeRegister            = 1,
    NodeUnregister          = 2,
    NodeHeartbeat           = 3,
    ServicePublish          = 4,
    ServiceSubscribe        = 5,
    TopologyUpdate          = 6,
    CrossHostSchedule       = 7,
    PskHandshake            = 8,
    PkiHandshake            = 9,
    AuthSuccess             = 10,
    AuthFailure             = 11,
    CredentialRotate        = 12,
    CmdExec                 = 13,
    FileTransfer            = 14,
    ConfigChange            = 15,
    AuditExport             = 16,
    WhitelistUpdate         = 17,  // v1.7 扩展
    RateLimitTriggered      = 18,  // v1.7 扩展
    ReplayDetected          = 19,  // v1.7 扩展
};

[[nodiscard]] inline const char* action_name(ActionType a) noexcept {
    switch (a) {
        case ActionType::NodeRegister:       return "node_register";
        case ActionType::NodeUnregister:     return "node_unregister";
        case ActionType::NodeHeartbeat:      return "node_heartbeat";
        case ActionType::ServicePublish:     return "service_publish";
        case ActionType::ServiceSubscribe:   return "service_subscribe";
        case ActionType::TopologyUpdate:     return "topology_update";
        case ActionType::CrossHostSchedule:  return "cross_host_schedule";
        case ActionType::PskHandshake:       return "psk_handshake";
        case ActionType::PkiHandshake:       return "pki_handshake";
        case ActionType::AuthSuccess:        return "auth_success";
        case ActionType::AuthFailure:        return "auth_failure";
        case ActionType::CredentialRotate:   return "credential_rotate";
        case ActionType::CmdExec:            return "cmd_exec";
        case ActionType::FileTransfer:       return "file_transfer";
        case ActionType::ConfigChange:       return "config_change";
        case ActionType::AuditExport:        return "audit_export";
        case ActionType::WhitelistUpdate:    return "whitelist_update";
        case ActionType::RateLimitTriggered: return "rate_limit_triggered";
        case ActionType::ReplayDetected:     return "replay_detected";
    }
    return "unknown";
}

struct AuditEvent {
    std::uint64_t sequence = 0;
    ActionType    action = ActionType::NodeHeartbeat;
    std::string   actor;
    std::string   target;
    std::string   params_json;  // UTF-8 JSON（轻量）
    std::int64_t  timestamp_ns = 0;
    std::string   prev_hash;
    std::string   params_hash;
};

class AuditLogger {
public:
    explicit AuditLogger(std::string path);
    ~AuditLogger() = default;

    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
    AuditLogger(AuditLogger&&) = delete;
    AuditLogger& operator=(AuditLogger&&) = delete;

    /// 追加一条事件（hash chain + 持久化），返回 sequence
    [[nodiscard]] core::Result<std::uint64_t>
    append(ActionType action, std::string actor, std::string target,
           std::string params_json) noexcept;

    /// 回放校验（顺序读 + hash 续接）
    [[nodiscard]] core::Result<bool>
    verify_chain() noexcept;

    /// 当前 sequence（>=0）
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    static std::string compute_params_hash(const std::string& json) noexcept;
    std::string        path_;
    mutable std::mutex mtx_;
    std::uint64_t      seq_ = 0;
    std::string        prev_hash_;  // 64-hex SHA-512
};

}  // namespace udaf::audit

#endif  // UDAF_AUDIT_AUDIT_HPP