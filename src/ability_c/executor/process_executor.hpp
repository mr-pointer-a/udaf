// process_executor.hpp - fork+exec 进程执行器
//
// 设计要点（性能契约 #23）：
//   - fork+exec ≤ 80ms
//   - 白名单命令（防止 shell 注入）
//   - 不抛异常（CLAUDE.md §3.5）

#ifndef UDAF_ABILITY_C_EXECUTOR_PROCESS_EXECUTOR_HPP
#define UDAF_ABILITY_C_EXECUTOR_PROCESS_EXECUTOR_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_c::executor {

struct ProcessExecutor {
    struct Options {
        std::string executable;
        std::vector<std::string> args;
        std::uint32_t timeout_ms = 5000;
        std::vector<std::string> allowed_executables;  // 白名单
    };

    struct Result {
        std::int32_t exit_code = -1;
        std::string stdout_text;
        std::string stderr_text;
        std::uint64_t elapsed_ns = 0;
    };

    /// 同步执行
    [[nodiscard]] static core::Result<Result>
    execute(const Options& opts) noexcept;
};

}  // namespace udaf::ability_c::executor

#endif  // UDAF_ABILITY_C_EXECUTOR_PROCESS_EXECUTOR_HPP