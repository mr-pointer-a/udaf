// test_result_props.cpp - Result<T> 单子属性测试
//
// 不变量：
//   - is_ok ↔ ¬is_err ∨ is_uninitialized（独立状态互斥）
//   - ok() 与 err() 构造后状态查询正确
//   - map 在 Ok 时应用 fn，在 Err 时透传
//   - and_then 在 Ok 时应用 fn，在 Err 时透传错误码
//   - value_or 在 Err 时返回 fallback

#include <cstdint>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "core/error_code.hpp"
#include "core/result.hpp"

using udaf::core::Result;
using udaf::core::ErrorCode;

namespace rc {

// 为 udaf::core::ErrorCode 提供任意值生成器。
// 仅生成有效的非零错误码；OK 用作哨兵值（在属性测试中显式跳过）。
template <>
struct Arbitrary<udaf::core::ErrorCode> {
    static Gen<udaf::core::ErrorCode> arbitrary() {
        return gen::map(gen::inRange<std::uint16_t>(1, 0xFFFF),
                        [](std::uint16_t v) {
                            return static_cast<udaf::core::ErrorCode>(v);
                        });
    }
};

}  // namespace rc

RC_GTEST_PROP(ResultProps, OkImpliesNotErr, (int value)) {
    auto r = Result<int>::ok(value);
    RC_ASSERT(r.is_ok());
    RC_ASSERT(!r.is_err());
    RC_ASSERT(!r.is_uninitialized());
    RC_ASSERT(r.value() == value);
}

RC_GTEST_PROP(ResultProps, ErrImpliesNotOk, (const ErrorCode& ec)) {
    if (ec == ErrorCode::OK) {
        RC_SUCCEED("Skip OK error code");
        return;
    }
    auto r = Result<int>::err(ec);
    RC_ASSERT(r.is_err());
    RC_ASSERT(!r.is_ok());
    RC_ASSERT(r.error() == ec);
}

RC_GTEST_PROP(ResultProps, MapPreservesErrCode, (const ErrorCode& ec)) {
    if (ec == ErrorCode::OK) {
        RC_SUCCEED("Skip OK error code");
        return;
    }
    auto r = Result<int>::err(ec);
    auto mapped = r.map([](int x) { return x * 2; });
    RC_ASSERT(mapped.is_err());
    RC_ASSERT(mapped.error() == ec);
}

RC_GTEST_PROP(ResultProps, AndThenPreservesErrCode, (const ErrorCode& ec)) {
    if (ec == ErrorCode::OK) {
        RC_SUCCEED("Skip OK error code");
        return;
    }
    auto r = Result<int>::err(ec);
    auto chained = r.and_then([](int x) {
        return Result<int>::ok(x + 1);
    });
    RC_ASSERT(chained.is_err());
    RC_ASSERT(chained.error() == ec);
}

RC_GTEST_PROP(ResultProps, MapAppliesFnOnOk, (int value)) {
    auto r = Result<int>::ok(value);
    auto mapped = r.map([](int x) { return x * 2; });
    RC_ASSERT(mapped.is_ok());
    RC_ASSERT(mapped.value() == value * 2);
}

RC_GTEST_PROP(ResultProps, ValueOrFallbackOnErr, (const ErrorCode& ec, int fallback)) {
    if (ec == ErrorCode::OK) {
        RC_SUCCEED("Skip OK error code");
        return;
    }
    auto r = Result<int>::err(ec);
    RC_ASSERT(r.value_or(fallback) == fallback);
}

RC_GTEST_PROP(ResultProps, ValueOrReturnsOkOnOk, (int value, int fallback)) {
    auto r = Result<int>::ok(value);
    RC_ASSERT(r.value_or(fallback) == value);
}

RC_GTEST_PROP(ResultProps, ChainedAndThenMonadicLaw, (int value)) {
    // 单子左单位元：Result::ok(x).and_then(f) ≡ f(x)
    auto r = Result<int>::ok(value);
    auto f = [](int x) { return Result<int>::ok(x + 10); };
    auto direct = f(value);
    auto chained = r.and_then(f);
    RC_ASSERT(chained.is_ok() && direct.is_ok());
    RC_ASSERT(chained.value() == direct.value());
}

RC_GTEST_PROP(ResultProps, ResultVoidOkIsOk, ()) {
    auto r = Result<void>::ok();
    RC_ASSERT(r.is_ok());
    RC_ASSERT(!r.is_err());
}

RC_GTEST_PROP(ResultProps, ResultVoidErrPreservesCode, (const ErrorCode& ec)) {
    if (ec == ErrorCode::OK) {
        RC_SUCCEED("Skip OK error code");
        return;
    }
    auto r = Result<void>::err(ec);
    RC_ASSERT(r.is_err());
    RC_ASSERT(r.error() == ec);
}
