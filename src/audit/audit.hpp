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

/// @brief 审计动作类型枚举，定义 19 项需要被持久化审计的事件类别。
///
/// 编号 1~16 来源于 ADR-006 §2.1；编号 17~19 为 v1.7 扩展（白名单更新、
/// 频率限制触发、重放检测）。枚举值使用 std::uint8_t 存储以节省磁盘空间。
enum class ActionType : std::uint8_t {
    NodeRegister            = 1,   ///< 节点注册事件
    NodeUnregister          = 2,   ///< 节点注销事件
    NodeHeartbeat           = 3,   ///< 节点心跳事件
    ServicePublish          = 4,   ///< 服务发布事件
    ServiceSubscribe        = 5,   ///< 服务订阅事件
    TopologyUpdate          = 6,   ///< 拓扑图更新事件
    CrossHostSchedule       = 7,   ///< 跨主机节点调度事件
    PskHandshake            = 8,   ///< PSK 握手成功事件
    PkiHandshake            = 9,   ///< PKI 握手成功事件
    AuthSuccess             = 10,  ///< 认证成功事件
    AuthFailure             = 11,  ///< 认证失败事件
    CredentialRotate        = 12,  ///< 凭据轮换事件
    CmdExec                 = 13,  ///< 命令执行事件
    FileTransfer            = 14,  ///< 文件传输事件
    ConfigChange            = 15,  ///< 配置变更事件
    AuditExport             = 16,  ///< 审计导出事件
    WhitelistUpdate         = 17,  ///< 白名单条目更新（v1.7 新增）
    RateLimitTriggered      = 18,  ///< 频率限制触发（v1.7 新增）
    ReplayDetected          = 19,  ///< 重放攻击检测（v1.7 新增）
};

/// @brief 将 ActionType 枚举转换为稳定的 snake_case 字符串。
///
/// 用于审计日志的 wire 格式与可读性输出；保证向前兼容的字符串字面量。
/// @param a 任意 ActionType 枚举值
/// @return 字符串字面量（生命周期 = static，永不返回 nullptr）；
///         理论上 switch 已覆盖全部枚举值，但加防御性返回 "unknown"。
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

/// @brief 单条审计事件的内存表示，从持久化日志读回或写入前的中间结构。
///
/// 字段对应审计日志的一行 JSON 记录（line-delimited JSON）。
struct AuditEvent {
    std::uint64_t sequence = 0;       ///< 全局递增序列号（从 1 开始，0 = 创世前）
    ActionType    action = ActionType::NodeHeartbeat;  ///< 事件类别
    std::string   actor;              ///< 触发主体（节点 ID / 用户 ID）
    std::string   target;             ///< 操作目标（节点 ID / 资源名）
    std::string   params_json;        ///< 事件参数（UTF-8 line-delimited JSON 子集）
    std::int64_t  timestamp_ns = 0;   ///< 单调时钟纳秒戳（CLOCK_MONOTONIC）
    std::string   prev_hash;          ///< 上一条事件的 128-hex SHA-512（创世条为空）
    std::string   params_hash;        ///< 当前事件的 128-hex SHA-512(params_json)
};

/// @brief 异步持久化的审计日志器，提供 hash chain 防篡改保证。
///
/// 设计要点（ADR-006 §2.1）：
///   - hash chain：prev_hash || params_hash → SHA-512
///   - 创世 hash：SHA-512(NodeId || boot_random || boot_time)
///   - 异步写入，性能契约 #27 ≥ 1000 条/秒
///   - 默认路径权限 0750（目录）+ 0640（文件）
///
/// 线程安全：append / verify_chain / sequence 均在内部 mutex 保护下，
/// 支持多线程并发追加。
class AuditLogger {
public:
    /// @brief 构造审计日志器并初始化底层文件。
    /// @param path 日志文件路径；目录不存在时尝试创建（0750）
    /// @throws 不抛异常；底层 IO 错误延迟到首次 append 报告
    explicit AuditLogger(std::string path);

    /// @brief 默认析构：flush 并关闭底层文件句柄（无异常）。
    ~AuditLogger() = default;

    // 不可拷贝/移动：持有内部 mutex 与文件句柄，禁止移动以避免竞态
    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
    AuditLogger(AuditLogger&&) = delete;
    AuditLogger& operator=(AuditLogger&&) = delete;

    /// @brief 追加一条审计事件，自动串联 prev_hash 并持久化。
    /// @param action 事件类别
    /// @param actor 触发主体（节点 ID / 用户名），UTF-8
    /// @param target 操作目标（节点 ID / 资源名），UTF-8
    /// @param params_json 事件参数，UTF-8 line-delimited JSON 子集
    /// @return 成功返回新事件的 sequence（>=1）；失败返回 ErrorCode
    [[nodiscard]] core::Result<std::uint64_t>
    append(ActionType action, std::string actor, std::string target,
           std::string params_json) noexcept;

    /// @brief 回放验证整个审计日志的 hash chain 续接性。
    /// @return 成功返回 true（链完整）；失败返回 ErrorCode
    ///         （SERIALIZE_DECODE_FAILED / AUDIT_CHAIN_BROKEN 等）
    [[nodiscard]] core::Result<bool>
    verify_chain() noexcept;

    /// @brief 当前最大 sequence。
    /// @return sequence >= 0（空日志返回 0）
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    /// @brief 计算 params_json 的 SHA-512 哈希并返回 128-hex 字符串。
    static std::string compute_params_hash(const std::string& json) noexcept;

    std::string        path_;          ///< 日志文件路径
    mutable std::mutex mtx_;           ///< 保护 seq_/prev_hash_ 的互斥锁
    std::uint64_t      seq_ = 0;       ///< 当前最大 sequence
    std::string        prev_hash_;     ///< 上一条事件的 128-hex SHA-512
};

}  // namespace udaf::audit

#endif  // UDAF_AUDIT_AUDIT_HPP