// unique_fd.hpp - UDAF POSIX fd RAII 句柄（Rule of Five + EINTR 重试）
//
// 设计要点（04 §2.8.1）：
//   - RAII：构造获取 fd，析构自动 close
//   - Rule of Five：禁用拷贝 + noexcept 移动（CLAUDE.md §3.5 不抛异常）
//   - close 被信号打断时，TEMP_FAILURE_RETRY 自动重试（man 2 close 明确要求）
//   - 不抛异常
//   - 纯头文件（无 .cpp），所有逻辑 inline 即可
//
// 应用场景：替代裸 int fd 类型，避免 fd 泄漏；与 Wal::fd_ 配合使用。

#ifndef UDAF_PLATFORM_FS_UNIQUE_FD_HPP
#define UDAF_PLATFORM_FS_UNIQUE_FD_HPP

#include <unistd.h>

#include <cerrno>
#include <cstddef>

namespace udaf::platform::fs {

/// POSIX fd RAII 句柄。不可拷贝，可移动。
///
/// 行为保证：
///   - 析构自动调用 ::close(fd_)，且对 EINTR 重试（系统调用被信号打断）
///   - 移动后源对象变为 invalid（fd_ = -1）
///   - release() 释放所有权，调用方负责后续 close
///   - reset(new_fd) 关闭旧 fd 并持有新 fd；new_fd < 0 表示仅关闭
class UniqueFd {
public:
    /// 默认构造：持有 invalid fd。
    UniqueFd() noexcept : fd_(-1) {}

    /// 显式构造：接管传入的 fd。
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    /// 析构：若持有有效 fd，自动 ::close。
    ~UniqueFd() {
        if (fd_ >= 0) {
            // man 2 close："调用被信号打断时 close 可能失败 EINTR，但 fd 已被关闭
            // 或将关闭，重试是安全的；但 Linux 上 close 不会被 EINTR 中断（POSIX
            // 实现差异），统一用 retry loop 提升可移植性。"
            int ret = 0;
            do {
                ret = ::close(fd_);
            } while (ret < 0 && errno == EINTR);
        }
    }

    // ---------- Rule of Five ----------

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            // 先关闭自身的 fd
            if (fd_ >= 0) {
                int ret = 0;
                do {
                    ret = ::close(fd_);
                } while (ret < 0 && errno == EINTR);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // ---------- 值访问 ----------

    /// 返回底层 fd（不转移所有权）。
    [[nodiscard]] int get() const noexcept { return fd_; }

    /// 释放所有权并返回 fd；调用方负责后续 close。
    [[nodiscard]] int release() noexcept {
        const int old = fd_;
        fd_ = -1;
        return old;
    }

    /// 关闭当前 fd（若有效），然后持有 new_fd。
    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
            int ret = 0;
            do {
                ret = ::close(fd_);
            } while (ret < 0 && errno == EINTR);
        }
        fd_ = new_fd;
    }

    /// 检查是否持有有效 fd。
    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }

    /// 隐式 bool 转换：is_valid()。
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

private:
    int fd_;
};

}  // namespace udaf::platform::fs

#endif  // UDAF_PLATFORM_FS_UNIQUE_FD_HPP