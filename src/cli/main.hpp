// main.hpp - CLI 命令表对外接口（供单元测试使用）
//
// 暴露内容：
//   - ExitCode 枚举（13 项退出码）
//   - Command 结构体（name + handler + brief）
//   - command_table() 返回全部 14 个子命令的注册表
//
// main.cpp 中的 handler 实现保持不变。

#ifndef UDAF_CLI_MAIN_HPP
#define UDAF_CLI_MAIN_HPP

#include <string>
#include <unordered_map>
#include <vector>

namespace udaf::cli {

// 退出码（ADR-010 §3.4）
enum ExitCode : int {
    kOk             = 0,
    kUsage          = 1,
    kUnknownCmd     = 2,
    kAuth           = 3,
    kNet            = 4,
    kTimeout        = 5,
    kResourceBusy   = 6,
    kInvalidArg     = 7,
    kInternal       = 8,
    kNotImplemented = 9,
    kWhitelistDenied = 10,
    kPartial        = 11,
    kPermission     = 12,
    kConfig         = 13,
};

// 命令条目
struct Command {
    const char* name;
    int (*handler)(const std::vector<std::string>& args);
    const char* brief;
};

// 14 子命令注册表
const std::unordered_map<std::string, Command>& command_table() noexcept;

}  // namespace udaf::cli

#endif  // UDAF_CLI_MAIN_HPP
