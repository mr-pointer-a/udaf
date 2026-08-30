// logger.hpp - UDAF 日志门面（spdlog 后端 + 三态级别）
//
// 设计要点（03 §7.3.1 + 04 §2.1）：
//   - 全局唯一 Logger 单例（std::once_flag 保护）
//   - 6 个等级：trace / debug / info / warn / error / critical
//   - 输出目标：stdout（默认）/ file（可选）/ syslog（可选，阶段 B+ 启用）
//   - 等级可通过环境变量 UDAF_LOG_LEVEL / 配置文件覆盖（运行时热更新由阶段 C+ 实现）
//   - 线程安全（spdlog::logger 内部 mutex）
//
// 注意：CLAUDE.md §3.5 禁止异常；spdlog 错误默认吞咽（构造时不抛）。
//
// 实现后端：spdlog（异步非阻塞日志，避免业务线程被磁盘阻塞）
//
// 设计决策：使用 PIMPL 持有 spdlog::logger 指针，**不**在头文件 include spdlog。

#ifndef UDAF_CORE_LOG_LOGGER_HPP
#define UDAF_CORE_LOG_LOGGER_HPP

#include <cstdint>
#include <memory>
#include <string_view>

#include "error_code.hpp"
#include "result.hpp"

namespace udaf::core {

/// 日志等级（与 spdlog::level::level_enum 一一对应）。
/// 独立枚举避免在头文件暴露 spdlog 类型。
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};

/// Logger 配置（构造时传入）。
struct LoggerConfig {
    /// 默认等级：Info。
    LogLevel level = LogLevel::Info;

    /// 输出模式：false → stdout；true → 文件（path 必须非空）。
    bool to_file = false;

    /// 日志文件路径（to_file = true 时必填）。
    std::string_view file_path;

    /// 文件最大字节（仅 to_file = true 时生效）；0 = 不限制。
    std::uint64_t max_file_size_bytes = 0;

    /// 历史文件保留数（仅 to_file = true 时生效）；0 = 不轮转。
    std::uint32_t max_rotated_files = 0;

    /// 是否在日志中携带线程 ID（默认 false）。
    bool include_thread_id = false;

    /// 自定义 pattern（spdlog format string）；空 = 使用默认格式
    /// "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v" 之类
    std::string_view pattern;
};

/// Logger 单例门面。所有 UDAF 模块通过 Logger::instance() 获取。
class Logger {
public:
    /// 获取全局单例。首次调用自动初始化（默认配置）。
    [[nodiscard]] static Logger& instance() noexcept;

    /// 用指定配置初始化（必须在首次使用日志前调用）。
    /// 重复初始化：返回 ErrorCode::RESOURCE_BUSY（CLAUDE.md §3.5：不抛异常）。
    [[nodiscard]] Result<void> init(const LoggerConfig& config) noexcept;

    /// 重新设置日志等级（热更新）。
    void set_level(LogLevel level) noexcept;

    /// 关闭 Logger（释放 spdlog 资源）。
    void shutdown() noexcept;

    [[nodiscard]] LogLevel level() const noexcept;

    // ---------- 6 个等级的日志接口 ----------
    void trace(std::string_view msg) noexcept;
    void debug(std::string_view msg) noexcept;
    void info(std::string_view msg) noexcept;
    void warn(std::string_view msg) noexcept;
    void error(std::string_view msg) noexcept;
    void critical(std::string_view msg) noexcept;

    /// 便捷方法：记录一条带有 ErrorCode 上下文的日志。
    /// 自动格式化为 "msg | error_code=<hex> <category>:<name>"
    void log_with_error(LogLevel level, std::string_view msg, ErrorCode code) noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger();

private:
    Logger() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// 将 LogLevel 枚举转换为 spdlog::level（实现文件内定义，避免泄漏 spdlog 类型）。
namespace log_detail {
[[nodiscard]] int to_spdlog_level(LogLevel level) noexcept;
}

}  // namespace udaf::core

#endif  // UDAF_CORE_LOG_LOGGER_HPP