// process_executor.cpp - fork+exec 实现
#include "process_executor.hpp"

#include <fcntl.h>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

extern char** environ;

namespace udaf::ability_c::executor {

namespace {

bool in_whitelist(const std::string& exe,
                  const std::vector<std::string>& allow) {
    if (allow.empty()) return false;  // 默认拒绝
    return std::ranges::any_of(allow, [&exe](const std::string& a) {
        return a == exe;
    });
}

}  // namespace

core::Result<ProcessExecutor::Result>
ProcessExecutor::execute(const Options& opts) noexcept {
    if (opts.executable.empty()) {
        return core::Result<Result>::err(core::ErrorCode::INVALID_ARG);
    }
    if (!in_whitelist(opts.executable, opts.allowed_executables)) {
        return core::Result<Result>::err(core::ErrorCode::BIZ_AUTH_UNTRUSTED);
    }

    int out_pipe[2], err_pipe[2];
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        return core::Result<Result>::err(core::ErrorCode::INTERNAL);
    }

    auto t0 = std::chrono::steady_clock::now();

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, err_pipe[0]);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, out_pipe[1]);
    posix_spawn_file_actions_addclose(&fa, err_pipe[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(opts.executable.c_str()));
    for (const auto& a : opts.args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    int rc = ::posix_spawnp(&pid, opts.executable.c_str(), &fa, nullptr,
                            argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (rc != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        return core::Result<Result>::err(core::ErrorCode::INTERNAL);
    }

    // 读管道（简化：不带 timeout 实现）
    std::array<char, 4096> buf{};
    Result res;
    ssize_t n = 0;
    while ((n = ::read(out_pipe[0], buf.data(), buf.size())) > 0) {
        res.stdout_text.append(buf.data(), static_cast<std::size_t>(n));
    }
    while ((n = ::read(err_pipe[0], buf.data(), buf.size())) > 0) {
        res.stderr_text.append(buf.data(), static_cast<std::size_t>(n));
    }
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    else res.exit_code = -1;

    auto t1 = std::chrono::steady_clock::now();
    res.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    return core::Result<Result>::ok(std::move(res));
}

}  // namespace udaf::ability_c::executor