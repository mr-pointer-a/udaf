// main.cpp - CLI 入口
//
// 14 个子命令的注册表与 handler 实现在 cli.cpp / cli library 中，
// 本文件仅保留 main() 入口，便于测试直接调用 handler。

#include "cli/main.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        // 直接调用 help：避免依赖 cli.cpp 中的 cmd_help（匿名 namespace）
        std::cout <<
            "udaf - Unified Device & Application Framework\n"
            "用法: udaf <subcommand> [options]\n"
            "运行 'udaf help' 查看可用子命令。\n";
        return udaf::cli::kUsage;
    }
    std::string sub = argv[1];
    if (sub == "-h" || sub == "--help") {
        std::cout << "运行 'udaf help' 查看子命令帮助。\n";
        return udaf::cli::kOk;
    }
    std::vector<std::string> rest;
    for (int i = 2; i < argc; ++i) rest.emplace_back(argv[i]);

    const auto& tbl = udaf::cli::command_table();
    auto it = tbl.find(sub);
    if (it == tbl.end()) {
        std::cerr << "udaf: 未知子命令 '" << sub << "'" << std::endl;
        std::cerr << "运行 'udaf help' 查看可用子命令" << std::endl;
        return udaf::cli::kUnknownCmd;
    }
    return it->second.handler(rest);
}
