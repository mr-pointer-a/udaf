// test_logger.cpp - Logger 单例门面测试（共 5 用例）
#include "log/logger.hpp"

#include <gtest/gtest.h>
#include <fstream>

using udaf::core::Logger;
using udaf::core::LoggerConfig;
using udaf::core::LogLevel;
using udaf::core::ErrorCode;

class LoggerTest : public ::testing::Test {
protected:
    void TearDown() override {
        Logger::instance().shutdown();
    }
};

TEST_F(LoggerTest, InitDefaultSucceeds) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    auto r = Logger::instance().init(cfg);
    ASSERT_TRUE(r.is_ok()) << "init default should succeed";
}

TEST_F(LoggerTest, InitFileSucceeds) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Debug;
    cfg.to_file = true;
    cfg.file_path = "/tmp/udaf_test_logger.log";
    auto r = Logger::instance().init(cfg);
    ASSERT_TRUE(r.is_ok());

    Logger::instance().info("hello file");
    Logger::instance().shutdown();

    std::ifstream ifs("/tmp/udaf_test_logger.log");
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("hello file"), std::string::npos);
}

TEST_F(LoggerTest, SetLevelUpdatesLevel) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());
    EXPECT_EQ(Logger::instance().level(), LogLevel::Info);

    Logger::instance().set_level(LogLevel::Debug);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Debug);

    Logger::instance().set_level(LogLevel::Error);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Error);
}

TEST_F(LoggerTest, LogWithErrorAttachesCode) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Debug;
    cfg.to_file = true;
    cfg.file_path = "/tmp/udaf_test_log_error.log";
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());

    Logger::instance().log_with_error(LogLevel::Warn, "test op failed", ErrorCode::NET_TIMEOUT);
    Logger::instance().shutdown();

    std::ifstream ifs("/tmp/udaf_test_log_error.log");
    ASSERT_TRUE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("test op failed"), std::string::npos);
    EXPECT_NE(content.find("NET"), std::string::npos);
    EXPECT_NE(content.find("network timeout"), std::string::npos);
}

TEST_F(LoggerTest, SixLevelsCompileAndRun) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Trace;
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());

    Logger::instance().trace("trace msg");
    Logger::instance().debug("debug msg");
    Logger::instance().info("info msg");
    Logger::instance().warn("warn msg");
    Logger::instance().error("error msg");
    Logger::instance().critical("critical msg");
}

// 覆盖 logger.cpp:33-35 Off 等级 + to_spdlog_level 默认分支
TEST_F(LoggerTest, OffLevelInitializes) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Off;
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());
    Logger::instance().info("off-level info (should not appear)");
}

// 覆盖 logger.cpp:120 set_level 在 impl_==null 时 early return
TEST_F(LoggerTest, SetLevelBeforeInitIsNoop) {
    // 重新构造一个 Logger 实例不现实（单例），但可模拟：调用 shutdown 后 set_level
    Logger::instance().shutdown();
    // 此时 impl_ 仍存在（shutdown 不清理），但 spd_logger 已 reset
    Logger::instance().set_level(LogLevel::Debug);
    // 恢复默认状态
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());
}

// ===== 边界用例：补齐 init/shutdown 各分支 =====

TEST_F(LoggerTest, InitFileEmptyPathReturnsErr) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    cfg.to_file = true;
    cfg.file_path = "";  // 空路径
    auto r = Logger::instance().init(cfg);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::CONFIG_INVALID_VALUE);
}

TEST_F(LoggerTest, InitInvalidPathReturnsErr) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    cfg.to_file = true;
    cfg.file_path = "/nonexistent/dir/x/log.txt";  // 不可写路径
    auto r = Logger::instance().init(cfg);
    // spdlog 抛异常被吞 → 返回 CONFIG_INVALID_VALUE
    ASSERT_TRUE(r.is_err());
}

TEST_F(LoggerTest, InitCustomPattern) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    cfg.pattern = "[%l] %v";
    auto r = Logger::instance().init(cfg);
    ASSERT_TRUE(r.is_ok());
}

TEST_F(LoggerTest, InitWithThreadId) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    cfg.include_thread_id = true;
    auto r = Logger::instance().init(cfg);
    ASSERT_TRUE(r.is_ok());
}

TEST_F(LoggerTest, InitTwiceRebuildsSink) {
    LoggerConfig cfg;
    cfg.level = LogLevel::Info;
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());
    cfg.to_file = true;
    cfg.file_path = "/tmp/udaf_test_logger_2nd.log";
    ASSERT_TRUE(Logger::instance().init(cfg).is_ok());
    Logger::instance().shutdown();
    std::ifstream ifs("/tmp/udaf_test_logger_2nd.log");
    EXPECT_TRUE(ifs.is_open());
    ifs.close();
    std::remove("/tmp/udaf_test_logger_2nd.log");
}

TEST_F(LoggerTest, SetLevelAfterShutdown) {
    Logger::instance().shutdown();
    // impl_ = nullptr → set_level 应直接 return
    Logger::instance().set_level(LogLevel::Error);
    // 不会崩；后续 log 也会因 impl_=nullptr 静默丢弃
    Logger::instance().info("after shutdown");
}

TEST_F(LoggerTest, LogWithErrorAfterShutdown) {
    Logger::instance().shutdown();
    // impl_ = nullptr → log_with_error 应直接 return
    Logger::instance().log_with_error(LogLevel::Error, "msg",
                                        udaf::core::ErrorCode::NET_TIMEOUT);
}

TEST_F(LoggerTest, LevelBeforeInit) {
    Logger::instance().shutdown();
    EXPECT_EQ(Logger::instance().level(), LogLevel::Info);
}

// 覆盖 logger.cpp:32 LogLevel::Critical → spdlog::critical
TEST_F(LoggerTest, SetLevelCritical) {
    Logger::instance().set_level(LogLevel::Critical);
    EXPECT_EQ(Logger::instance().level(), LogLevel::Critical);
    Logger::instance().critical("test critical");
}

// 覆盖 logger.cpp:62-63 lazy impl_ 创建路径（先 shutdown 后 init）
TEST_F(LoggerTest, ReinitAfterShutdown) {
    Logger::instance().shutdown();
    // 重新初始化应触发 impl_ make_unique
    Logger::instance().init(LoggerConfig{});
    EXPECT_EQ(Logger::instance().level(), LogLevel::Info);
}

