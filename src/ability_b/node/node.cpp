// node.cpp - Scheduler 实现
#include "node.hpp"

namespace udaf::ability_b::node {

core::Result<void>
Scheduler::schedule(const std::string& /*node_id*/,
                    std::string_view /*target_host*/) noexcept {
    // 简化：仅检查白名单回调是否返回 true
    if (whitelist_check_) {
        // 调度时 source 通常为当前节点；此处传 host 当 source
        if (!whitelist_check_("", "")) {
            return core::Result<void>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
        }
    }
    return core::Result<void>::ok();
}

bool
Scheduler::is_allowed(std::string_view target, std::string_view source) const noexcept {
    if (!whitelist_check_) return false;
    return whitelist_check_(target, source);
}

}  // namespace udaf::ability_b::node