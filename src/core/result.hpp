// result.hpp - UDAF Result<T> 模板与 Result<void> 特化
//
// 设计目标（03 §8.0）：
//   - 三态：Ok(value) / Err(ErrorCode) / Uninitialized
//   - 8 个方法：ok/err/is_ok/is_err/value/error/and_then/map/or_else/on_error
//   - 全部 [[nodiscard]]
//   - Rule of Five：显式 =delete 拷贝 + noexcept 移动
//   - 不抛异常（CLAUDE.md §3.5）
//
// 与 ADR-011 §2.3 错误码集成：所有 Result<T> 实例可携带 ErrorCode。

#ifndef UDAF_CORE_RESULT_HPP
#define UDAF_CORE_RESULT_HPP

#include <cassert>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <variant>

#include "error_code.hpp"

namespace udaf::core {

/// Result<T> 主模板：成功值 T 或错误码。
/// 使用 std::variant 存储三态（uninitialized/ok/err），避免默认构造时存储随机值。
template <typename T>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = ErrorCode;

    // ---------- 工厂方法 ----------

    /// 构造 Ok 状态，持有值。
    [[nodiscard]] static Result ok(T value) {
        return Result(std::in_place_index<1>, std::move(value));
    }

    /// 构造 Err 状态，持有错误码。
    [[nodiscard]] static Result err(ErrorCode code) {
        return Result(std::in_place_index<2>, code);
    }

    // ---------- Rule of Five ----------

    Result() = default;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    // ---------- 状态查询 ----------

    /// @brief 当前是否处于 Ok 状态（持有 T 值）。
    [[nodiscard]] bool is_ok() const noexcept {
        return storage_.index() == 1;
    }

    /// @brief 当前是否处于 Err 状态（持有 ErrorCode）。
    [[nodiscard]] bool is_err() const noexcept {
        return storage_.index() == 2;
    }

    /// @brief 当前是否处于未初始化状态（默认构造或 move-from 后）。
    [[nodiscard]] bool is_uninitialized() const noexcept {
        return storage_.index() == 0;
    }

    /// @brief 上下文布尔转换：仅在 Ok 时为 true，等价于 is_ok()。
    [[nodiscard]] explicit operator bool() const noexcept {
        return is_ok();
    }

    // ---------- 值访问 ----------

    /// 返回 Ok 持有的值；若非 Ok 则终止（违反前置条件）。
    [[nodiscard]] T& value() & {
        assert(is_ok());
        return std::get<1>(storage_);
    }

    [[nodiscard]] const T& value() const& {
        assert(is_ok());
        return std::get<1>(storage_);
    }

    [[nodiscard]] T&& value() && {
        assert(is_ok());
        return std::get<1>(std::move(storage_));
    }

    /// 返回错误码；若非 Err 则返回 ErrorCode::INTERNAL。
    [[nodiscard]] ErrorCode error() const noexcept {
        if (storage_.index() == 2) {
            return std::get<2>(storage_);
        }
        return ErrorCode::INTERNAL;
    }

    /// 返回 Ok 持有的值或 fallback。
    template <typename U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return is_ok() ? std::get<1>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }

    // ---------- 单子操作（链式错误处理） ----------

    /// 若 Ok 则应用函数 fn(T) → Result<U>；若 Err 则原样传递。
    template <typename F>
    [[nodiscard]] auto and_then(F&& fn) const& -> std::invoke_result_t<F, const T&> {
        using ResultU = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return std::forward<F>(fn)(std::get<1>(storage_));
        }
        return ResultU::err(error());
    }

    template <typename F>
    [[nodiscard]] auto and_then(F&& fn) && -> std::invoke_result_t<F, T&&> {
        using ResultU = std::invoke_result_t<F, T&&>;
        if (is_ok()) {
            return std::forward<F>(fn)(std::get<1>(std::move(storage_)));
        }
        return ResultU::err(error());
    }

    /// 若 Ok 则应用函数 fn(T) → Result<U> 或 U（统一为 Result<U>）。
    template <typename F>
    [[nodiscard]] auto map(F&& fn) const& -> Result<std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        if (is_ok()) {
            return Result<U>::ok(std::forward<F>(fn)(std::get<1>(storage_)));
        }
        return Result<U>::err(error());
    }

    template <typename F>
    [[nodiscard]] auto map(F&& fn) && -> Result<std::invoke_result_t<F, T&&>> {
        using U = std::invoke_result_t<F, T&&>;
        if (is_ok()) {
            return Result<U>::ok(std::forward<F>(fn)(std::get<1>(std::move(storage_))));
        }
        return Result<U>::err(error());
    }

    /// 若 Err 则应用函数 fn(ErrorCode) → Result<T>；若 Ok 则原样传递。
    /// 用于错误恢复（返回 Ok 表示已恢复，返回 Err 表示新错误）。
    template <typename F>
    [[nodiscard]] Result or_else(F&& fn) const& {
        if (is_err()) {
            return std::forward<F>(fn)(error());
        }
        return *this;
    }

    template <typename F>
    [[nodiscard]] Result or_else(F&& fn) && {
        if (is_err()) {
            return std::forward<F>(fn)(error());
        }
        return std::move(*this);
    }

    /// 若 Err 则调用副作用 handler fn(ErrorCode)；返回 *this 用于链式调用。
    /// 与 or_else 区别：on_error 不修改错误状态，仅记录/上报。
    template <typename F>
    [[nodiscard]] const Result& on_error(F&& fn) const& {
        if (is_err()) {
            std::forward<F>(fn)(error());
        }
        return *this;
    }

    template <typename F>
    [[nodiscard]] Result&& on_error(F&& fn) && {
        if (is_err()) {
            std::forward<F>(fn)(error());
        }
        return std::move(*this);
    }

private:
    template <std::size_t I, typename U>
    Result(std::in_place_index_t<I> /*tag*/, U&& value)
        : storage_(std::in_place_index<I>, std::forward<U>(value)) {}

    // storage_ 三态：
    //   index 0: std::monostate（未初始化）
    //   index 1: T（Ok）
    //   index 2: ErrorCode（Err）
    std::variant<std::monostate, T, ErrorCode> storage_;
};

// ============================================================
// Result<void> 特化
// ============================================================

/// Result<void> 特化，仅承载 Ok/ Err 二态。
/// 与 Result<T> 区别：无 value() 方法（仅 ok()/err()/is_ok()/is_err()/error()）。
/// 适用于仅需表达"成功或某个失败原因"的纯命令式操作（如 init / start / stop）。
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;
    using error_type = ErrorCode;

    /// @brief 构造 Ok 状态。
    [[nodiscard]] static Result ok() {
        return {State::Ok};
    }

    /// @brief 构造 Err 状态。
    /// @param code 错误码，参见 ErrorCode 枚举
    [[nodiscard]] static Result err(ErrorCode code) {
        return {State::Err, code};
    }

    Result() = default;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    /// @brief 当前是否处于 Ok 状态。
    [[nodiscard]] bool is_ok() const noexcept { return state_ == State::Ok; }

    /// @brief 当前是否处于 Err 状态。
    [[nodiscard]] bool is_err() const noexcept { return state_ == State::Err; }

    /// @brief 上下文布尔转换：等价于 is_ok()。
    [[nodiscard]] explicit operator bool() const noexcept { return is_ok(); }

    /// @brief 返回错误码；若非 Err 则返回 ErrorCode::OK。
    [[nodiscard]] ErrorCode error() const noexcept { return error_; }

    /// 成功时无操作；失败时返回 fn(ErrorCode) 的 Result<void>（用于链式）。
    template <typename F>
    [[nodiscard]] Result or_else(F&& fn) & {
        if (is_err()) {
            return std::forward<F>(fn)(error_);
        }
        return std::move(*this);
    }

    template <typename F>
    [[nodiscard]] Result or_else(F&& fn) && {
        if (is_err()) {
            return std::forward<F>(fn)(error_);
        }
        return std::move(*this);
    }

    /// 失败时调用 handler（副作用型，不改变状态）。
    template <typename F>
    [[nodiscard]] Result& on_error(F&& fn) & {
        if (is_err()) {
            std::forward<F>(fn)(error_);
        }
        return *this;
    }

    template <typename F>
    [[nodiscard]] Result&& on_error(F&& fn) && {
        if (is_err()) {
            std::forward<F>(fn)(error_);
        }
        return std::move(*this);
    }

private:
    enum class State : std::uint8_t { Uninitialized, Ok, Err };

    Result(State s) : state_(s) {}
    Result(State s, ErrorCode e) : state_(s), error_(e) {}

    State state_ = State::Uninitialized;
    ErrorCode error_ = ErrorCode::OK;
};

}  // namespace udaf::core

#endif  // UDAF_CORE_RESULT_HPP