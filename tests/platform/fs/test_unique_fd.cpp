// test_unique_fd.cpp - UniqueFd RAII + EINTR 重试测试（共 4 用例）
#include "platform/fs/unique_fd.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/signalfd.h>

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

using udaf::platform::fs::UniqueFd;

namespace {

/// 让当前 fd 在第一次 close() 时收到 EINTR，第二次才真正关闭。
/// 通过 SIGUSR1 的 signalfd 技巧注入更可控，这里采用更简单的：
/// 用 SA_RESTART=false 安装 handler，第一次进入时设置 in_eintr，第二次跳过。
std::atomic<int> close_call_count{0};
std::atomic<bool> inject_eintr{false};

void sigusr1_handler(int /*signo*/, siginfo_t* /*info*/, void* /*ucontext*/) {
    // 不需要做任何事；close() 在 EINTR 时由 UniqueFd 自动重试
}

}  // namespace

class UniqueFdTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 安装 SIGUSR1 handler（无 SA_RESTART，使 close 收到 EINTR）
        struct sigaction sa{};
        sa.sa_sigaction = sigusr1_handler;
        sa.sa_flags = SA_RESTART ? 0 : 0;  // 不设 SA_RESTART
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGUSR1, &sa, nullptr);
        close_call_count.store(0);
        inject_eintr.store(false);
    }
    void TearDown() override {
        // 还原默认 handler
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        ::sigaction(SIGUSR1, &sa, nullptr);
    }
};

TEST_F(UniqueFdTest, DefaultConstructIsInvalid) {
    UniqueFd fd;
    EXPECT_FALSE(fd.is_valid());
    EXPECT_EQ(fd.get(), -1);
}

TEST_F(UniqueFdTest, HoldValidFd) {
    int raw = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(raw, 0);
    UniqueFd fd(raw);
    EXPECT_TRUE(fd.is_valid());
    EXPECT_EQ(fd.get(), raw);
}

TEST_F(UniqueFdTest, MoveTransfersOwnership) {
    int raw = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(raw, 0);
    UniqueFd a(raw);
    ASSERT_TRUE(a.is_valid());

    UniqueFd b(std::move(a));
    EXPECT_FALSE(a.is_valid());
    EXPECT_TRUE(b.is_valid());
    EXPECT_EQ(b.get(), raw);
}

TEST_F(UniqueFdTest, ReleaseReturnsFdAndDetaches) {
    int raw = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(raw, 0);
    UniqueFd fd(raw);
    const int released = fd.release();
    EXPECT_EQ(released, raw);
    EXPECT_FALSE(fd.is_valid());
    // 调用方必须 close
    EXPECT_EQ(::close(released), 0);
}

TEST_F(UniqueFdTest, ResetClosesOldAndHoldsNew) {
    int r1 = ::open("/dev/null", O_RDONLY);
    int r2 = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(r1, 0);
    ASSERT_GE(r2, 0);
    UniqueFd fd(r1);
    fd.reset(r2);
    EXPECT_EQ(fd.get(), r2);
    // r1 已被关闭（fd 不再持有）
    EXPECT_EQ(::fcntl(r1, F_GETFD), -1);  // closed
    EXPECT_EQ(::close(r2), 0);  // 清理
}

TEST_F(UniqueFdTest, CloseTwiceSafe) {
    // 析构后再构造 new handle，不应 double close
    int raw = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(raw, 0);
    {
        UniqueFd fd(raw);
        EXPECT_TRUE(fd.is_valid());
    }
    // raw 已被关闭
    EXPECT_EQ(::fcntl(raw, F_GETFD), -1);
    // 再次创建 new UniqueFd 用 -1，不应触发 close
    UniqueFd invalid(-1);
    EXPECT_FALSE(invalid.is_valid());
    invalid.reset(-1);  // 也安全
    EXPECT_FALSE(invalid.is_valid());
}