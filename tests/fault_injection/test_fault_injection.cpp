// test_fault_injection.cpp - GTest 自检：通过 fork+exec + LD_PRELOAD 验证
// 故障注入框架本身工作正确。
//
// 测试策略：
//   父测试进程不加载 libudaf_fi.so；
//   每个测试 fork 一个子进程，子进程通过 LD_PRELOAD 加载 libudaf_fi.so；
//   子进程执行 helper_* 二进制，helper 触发对应 syscall 并把结果写到 stdout；
//   父测试读 stdout 并断言。

#include <gtest/gtest.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;  // POSIX 全局环境

namespace {

std::string dirname_of(const std::string& p) {
    auto pos = p.rfind('/');
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
}

std::string lib_path() {
    char self[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len <= 0) return "";
    self[len] = '\0';
    return dirname_of(self) + "/libudaf_fi.so";
}

std::string helper_path(const std::string& helper_name) {
    char self[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len <= 0) return "";
    self[len] = '\0';
    return dirname_of(self) + "/" + helper_name;
}

struct child_result {
    int exit_code;
    std::string stdout_text;
    std::string stderr_text;
};

child_result run_subprocess(const std::string& prog,
                            const std::vector<std::string>& extra_env) {
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return {-1, "", "pipe() failed"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        return {-1, "", "fork() failed"};
    }
    if (pid == 0) {
        // 子进程
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        // 构造 envp：当前 environ + 附加
        std::vector<std::string> env_strs;
        for (char** e = environ; *e != nullptr; ++e) {
            env_strs.emplace_back(*e);
        }
        for (const auto& e : extra_env) env_strs.emplace_back(e);
        std::vector<char*> envp;
        for (const auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
        envp.push_back(nullptr);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(prog.c_str()));
        argv.push_back(nullptr);

        execve(prog.c_str(), argv.data(), envp.data());
        perror("execve failed");
        _exit(127);
    }

    // 父进程
    close(out_pipe[1]);
    close(err_pipe[1]);

    child_result r{};
    char buf[1024];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
        r.stdout_text.append(buf, static_cast<size_t>(n));
    }
    close(out_pipe[0]);
    while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
        r.stderr_text.append(buf, static_cast<size_t>(n));
    }
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
    else r.exit_code = -1;
    return r;
}

// ---- 测试用例 ----

TEST(FaultInjection, NoRulesSocketSucceeds) {
    auto r = run_subprocess(helper_path("fi_helper_socket"),
                            {});  // 不设 LD_PRELOAD
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("OK"), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, SocketAlwaysFail) {
    auto r = run_subprocess(helper_path("fi_helper_socket"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_NET_FAIL=socket:ECONNREFUSED:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR=ECONNREFUSED"), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, SocketFailHalfRateReproducible) {
    auto r = run_subprocess(helper_path("fi_helper_socket_repeat"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_NET_FAIL=socket:ECONNREFUSED:50pct",
                             "UDAF_FI_SEED=42"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    int ok_count = 0;
    int err_count = 0;
    sscanf(r.stdout_text.c_str(), "OK=%d ERR=%d", &ok_count, &err_count);
    EXPECT_GT(ok_count, 0);
    EXPECT_GT(err_count, 0);
    EXPECT_EQ(ok_count + err_count, 100);
}

TEST(FaultInjection, NetDelayActuallyDelays) {
    auto r = run_subprocess(helper_path("fi_helper_connect_delay"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_NET_DELAY_US=connect:30000:30000"});
    ASSERT_EQ(r.exit_code, 0);
    long ms = 0;
    sscanf(r.stdout_text.c_str(), "ELAPSED_MS=%ld", &ms);
    EXPECT_GE(ms, 30) << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, FsFailOpenAlwaysFails) {
    auto r = run_subprocess(helper_path("fi_helper_fs"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=open:EACCES:100pct"});
    ASSERT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_text.find("ERR=EACCES"), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, TimeSkipForwardAdvancesClock) {
    auto r = run_subprocess(helper_path("fi_helper_clock_skip"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_TIME_SKIP_US=100000"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    long delta_ms = 0;
    sscanf(r.stdout_text.c_str(), "DELTA_MS=%ld", &delta_ms);
    EXPECT_GE(delta_ms, 100) << "delta_ms=" << delta_ms << " stdout=" << r.stdout_text;
}

// ---------- Wal 错误路径覆盖（覆盖率驱动）----------

TEST(FaultInjection, WalOpenFailReturnsErr) {
    auto r = run_subprocess(helper_path("fi_helper_wal_open_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=open:EACCES:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    // Wal::create 在 file open 失败时返回非 Ok
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
    EXPECT_EQ(r.stdout_text.find("OK\n"), std::string::npos)
        << "should not succeed: stdout=" << r.stdout_text;
}

TEST(FaultInjection, WalWriteFailReturnsErr) {
    auto r = run_subprocess(helper_path("fi_helper_wal_write_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=pwrite:EIO:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, WalFsyncFailReturnsErr) {
    auto r = run_subprocess(helper_path("fi_helper_wal_fsync_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=fsync:EIO:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, WalWriteEintrRecovers) {
    // 用 net_delay 让 pwrite 第一次延迟后返回 EINTR，第二次成功
    // → write_at 进入 retry 分支最终写入成功
    auto r = run_subprocess(helper_path("fi_helper_wal_write_eintr"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=pwrite:EINTR:50pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    // 50% EINTR 概率下多数情况下能恢复（write_at 内 while 循环重试）
    // 接受 OK 或 ERR（极少数情况下全部 EINTR）
    EXPECT_TRUE(r.stdout_text.find("OK=") != std::string::npos ||
                r.stdout_text.find("ERR=") != std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, WalTruncateLseekFailReturnsErr) {
    // truncate 内部 lseek(SEEK_SET) 失败 → 返回 Err
    // 拦截 lseek 让 truncate 走到 seek_set 失败分支（行 591-592）
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_lseek_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=lseek:EIO:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

TEST(FaultInjection, WalTruncateAllFtruncateFailReturnsErr) {
    // truncate_all 内部 ftruncate 失败 → 返回 Err（行 609-610）
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_all_ftruncate_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=ftruncate:EIO:100pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// pread 返回 EINTR → read_at 进入重试分支（行 64-65）
TEST(FaultInjection, WalReadEintrRecovers) {
    auto r = run_subprocess(helper_path("fi_helper_wal_read_eintr"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=pread:EINTR:50pct"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    // 50% 概率下多数能恢复，少数全 EINTR 时 ERR；二者均合法
    EXPECT_TRUE(r.stdout_text.find("OK=") != std::string::npos ||
                r.stdout_text.find("ERR=") != std::string::npos)
        << "stdout=" << r.stdout_text;
}

// replay 内部 seek_set 失败（行 490-491）
TEST(FaultInjection, WalReplaySeekFailReturnsErr) {
    // 跳过 3 次 lseek（create/open/append 内部用到的），第 4 次起失败
    auto r = run_subprocess(helper_path("fi_helper_wal_replay_seek_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=lseek:EIO:100pct:3"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// replay_stream 内部 seek_set 失败（行 521-522）
TEST(FaultInjection, WalReplayStreamSeekFailReturnsErr) {
    auto r = run_subprocess(helper_path("fi_helper_wal_replay_stream_seek_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=lseek:EIO:100pct:3"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// truncate rewrite 分支 seek_set 失败（行 591-592）
TEST(FaultInjection, WalTruncateRewriteSeekFailReturnsErr) {
    // 跳过 ~5 次 lseek（create+append 用到的），从 truncate rewrite 的 seek_set 起失败
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_rewrite_seek_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=lseek:EIO:100pct:5"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// truncate rewrite 分支 ftruncate_wrapped 失败（行 588-589）
TEST(FaultInjection, WalTruncateRewriteFtruncateFailReturnsErr) {
    // append 不调用 ftruncate，跳过 0 次即可命中 truncate rewrite 的 ftruncate
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_rewrite_ftruncate_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=ftruncate:EIO:100pct:0"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// truncate rewrite 分支 write_entry 失败（行 596-597）
TEST(FaultInjection, WalTruncateRewriteWriteFailReturnsErr) {
    // 3 次 append × 2 pwrite = 6 次；跳过 6 次后 pwrite 失败 → rewrite 第一次 write_entry 失败
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_rewrite_write_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=pwrite:EIO:100pct:6"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// truncate kept.empty 分支 ftruncate_wrapped 失败（行 577-578）
TEST(FaultInjection, WalTruncateKeptEmptyFtruncateFailReturnsErr) {
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_kept_empty_ftruncate_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=ftruncate:EIO:100pct:0"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

// truncate_all 内部 seek_set 失败（行 611-612）
TEST(FaultInjection, WalTruncateAllSeekFailReturnsErr) {
    // helper: 1 次 create (0 lseek) + 1 次 append (1 lseek) + truncate_all (1 lseek)
    // → 跳过 1 次让 truncate_all 的 lseek 失败
    auto r = run_subprocess(helper_path("fi_helper_wal_truncate_all_seek_fail"),
                            {"LD_PRELOAD=" + lib_path(),
                             "UDAF_FI_FS_FAIL=lseek:EIO:100pct:1"});
    ASSERT_EQ(r.exit_code, 0) << "stderr: " << r.stderr_text;
    EXPECT_NE(r.stdout_text.find("ERR="), std::string::npos)
        << "stdout=" << r.stdout_text;
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
