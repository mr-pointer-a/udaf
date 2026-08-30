// peer_whitelist.hpp - 设备端跨主机节点调度白名单
//
// 设计要点（CLAUDE.md §3.7 跨主机节点调度必须白名单）：
//   - 字段：fingerprint_sha256_（32 字节）+ allowed_capabilities_
//   - add/remove/check 三方法
//   - 检查语义：节点在白名单且 capability 在允许列表
//   - 不抛异常（CLAUDE.md §3.5）
//
// 设计依据：docs/04-module-design.md §2.2 + §3 trust subdirectory

#ifndef UDAF_ABILITY_A_TRUST_PEER_WHITELIST_HPP
#define UDAF_ABILITY_A_TRUST_PEER_WHITELIST_HPP

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_a::trust {

/// 白名单条目
struct WhitelistEntry {
    std::string node_id;
    std::vector<std::uint8_t> fingerprint_sha256_;  // 32 字节
    std::unordered_set<std::string> allowed_capabilities_;
};

/// 跨主机调度白名单
class PeerWhitelist {
public:
    PeerWhitelist() = default;
    ~PeerWhitelist() = default;

    PeerWhitelist(const PeerWhitelist&) = delete;
    PeerWhitelist& operator=(const PeerWhitelist&) = delete;
    PeerWhitelist(PeerWhitelist&&) = delete;
    PeerWhitelist& operator=(PeerWhitelist&&) = delete;

    /// 添加/覆盖节点
    /// @return Ok(true) 新增；Ok(false) 更新
    [[nodiscard]] core::Result<bool>
    add(const WhitelistEntry& entry) noexcept;

    /// 移除节点
    /// @return Ok(true) 移除成功；Ok(false) 节点不在
    [[nodiscard]] core::Result<bool>
    remove(std::string_view node_id) noexcept;

    /// 检查节点是否在白名单（可选校验 capability）
    [[nodiscard]] bool
    contains(std::string_view node_id,
             std::string_view capability = {}) const noexcept;

    /// 获取条目
    [[nodiscard]] core::Result<WhitelistEntry>
    get(std::string_view node_id) const noexcept;

    /// 节点数
    [[nodiscard]] std::size_t size() const noexcept;

    /// 清空（仅测试）
    void clear() noexcept;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, WhitelistEntry> entries_;
};

}  // namespace udaf::ability_a::trust

#endif  // UDAF_ABILITY_A_TRUST_PEER_WHITELIST_HPP