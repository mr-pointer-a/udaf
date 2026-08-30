// logger.cpp - spdlog 后端实现（PIMPL）
#include "log/logger.hpp"

#include <spdlog/common.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <limits>

#include <cstdio>
#include <mutex>
#include <string>

namespace udaf::core {

// ---------- PIMPL 实现 ----------
struct Logger::Impl {
    std::shared_ptr<spdlog::logger> spd_logger;
    LogLevel level = LogLevel::Info;
    bool initialized = false;
};

namespace log_detail {
int to_spdlog_level(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace: return spdlog::level::trace;
    case LogLevel::Debug: return spdlog::level::debug;
    case LogLevel::Info: return spdlog::level::info;
    case LogLevel::Warn: return spdlog::level::warn;
    case LogLevel::Error: return spdlog::level::err;
    case LogLevel::Critical: return spdlog::level::critical;
    case LogLevel::Off: return spdlog::level::off;
    }
    return spdlog::level::info;
}
}  // namespace log_detail

// ---------- 单例 ----------

Logger& Logger::instance() noexcept {
    static Logger inst;
    // 每次调用前检查 impl_ 是否就绪：std::call_once 会阻塞测试间 shutdown/init 的灵活性。
    // 这里改用 lazy-init + shutdown 兼容：首次调用或 shutdown 后重新创建默认 Impl。
    if (!inst.impl_) {
        inst.impl_ = std::make_unique<Impl>();
        // 默认 stdout sink + 默认等级 Info
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        inst.impl_->spd_logger = std::make_shared<spdlog::logger>("udaf", sink);
        inst.impl_->spd_logger->set_level(spdlog::level::info);
        inst.impl_->spd_logger->flush_on(spdlog::level::trace);
        inst.impl_->initialized = true;
    }
    return inst;
}

Result<void> Logger::init(const LoggerConfig& config) noexcept {
    // 总是重新创建 sink（保证测试/CLI 切换 file ↔ stdout 生效；生产环境 Logger
    // 建议在 main 入口调用一次 init() 后不再切换）。
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    if (impl_->spd_logger) {
        impl_->spd_logger->flush();
        impl_->spd_logger.reset();
    }

    std::shared_ptr<spdlog::sinks::sink> sink;
    try {
        if (config.to_file) {
            if (config.file_path.empty()) {
                return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
            }
            // 使用 rotating_file_sink：若未启用轮转则用 numeric_limits 上限 + 1 个文件
            const std::uint64_t size_cap = config.max_file_size_bytes > 0
                ? config.max_file_size_bytes
                : std::numeric_limits<std::uint64_t>::max();
            const std::uint32_t files = config.max_rotated_files > 0
                ? config.max_rotated_files
                : 1;
            sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                std::string(config.file_path),
                size_cap,
                files);
        } else {
            sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        }
    } catch (const std::exception& e) {
        // spdlog 抛异常表示路径无效等；CLAUDE.md §3.5 不允许异常，但 spdlog 内部会抛
        // 我们无法控制；只能吞咽并返回错误
        std::fprintf(stderr, "udaf::core::Logger init sink failed: %s\n", e.what());
        return Result<void>::err(ErrorCode::CONFIG_INVALID_VALUE);
    }

    if (!config.pattern.empty()) {
        sink->set_pattern(std::string(config.pattern));
    } else {
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    }

    impl_->spd_logger = std::make_shared<spdlog::logger>("udaf", sink);
    spdlog::level::level_enum lv =
        static_cast<spdlog::level::level_enum>(log_detail::to_spdlog_level(config.level));
    impl_->spd_logger->set_level(lv);
    // flush_on(trace) 保证所有等级日志立即落盘（避免测试/审计场景漏丢）
    impl_->spd_logger->flush_on(spdlog::level::trace);
    if (config.include_thread_id) {
        impl_->spd_logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid %t] %v");
    }
    impl_->level = config.level;
    impl_->initialized = true;
    spdlog::set_default_logger(impl_->spd_logger);
    return Result<void>::ok();
}

void Logger::set_level(LogLevel level) noexcept {
    if (!impl_ || !impl_->spd_logger) {
        return;
    }
    impl_->spd_logger->set_level(
        static_cast<spdlog::level::level_enum>(log_detail::to_spdlog_level(level)));
    impl_->level = level;
}

void Logger::shutdown() noexcept {
    if (impl_ && impl_->spd_logger) {
        impl_->spd_logger->flush();
    }
    // 不调用 spdlog::shutdown()：它会清空全局 logger 注册表，影响其他模块。
    // 仅清空本实例 impl_，下次 instance() 会重新创建。
    impl_.reset();
}

LogLevel Logger::level() const noexcept {
    return impl_ ? impl_->level : LogLevel::Info;
}

void Logger::trace(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->trace(msg);
}

void Logger::debug(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->debug(msg);
}

void Logger::info(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->info(msg);
}

void Logger::warn(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->warn(msg);
}

void Logger::error(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->error(msg);
}

void Logger::critical(std::string_view msg) noexcept {
    if (impl_ && impl_->spd_logger) impl_->spd_logger->critical(msg);
}

void Logger::log_with_error(LogLevel level, std::string_view msg, ErrorCode code) noexcept {
    if (!impl_ || !impl_->spd_logger) return;
    const auto fmt = std::string(msg) + " | error_code=0x" +
        [&] {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(code));
            return std::string(buf);
        }() +
        " " + category_of(code) + ":" + to_string(code);

    spdlog::level::level_enum lv =
        static_cast<spdlog::level::level_enum>(log_detail::to_spdlog_level(level));
    impl_->spd_logger->log(lv, fmt);
}

Logger::~Logger() = default;

}  // namespace udaf::core