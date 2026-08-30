// test_result_t.cpp - Result<T> 主模板测试（共 12 用例）
// 覆盖 docs/05-test-plan.md §5.3.1 中 Result<T> 全部方法 + 边界
#include "result.hpp"
#include "error_code.hpp"

#include <gtest/gtest.h>
#include <string>

using udaf::core::ErrorCode;
using udaf::core::Result;

namespace {

// ---------- 构造与基础状态 ----------

TEST(ResultT, OkConstructIsOk) {
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_FALSE(r.is_uninitialized());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultT, ErrConstructIsErr) {
    auto r = Result<int>::err(ErrorCode::INVALID_ARG);
    EXPECT_FALSE(r.is_ok());
    EXPECT_TRUE(r.is_err());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultT, DefaultConstructIsUninitialized) {
    Result<int> r;
    EXPECT_FALSE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_TRUE(r.is_uninitialized());
}

TEST(ResultT, ValueAccessReturnsOk) {
    auto r = Result<int>::ok(123);
    EXPECT_EQ(r.value(), 123);
}

TEST(ResultT, ErrorAccessReturnsCode) {
    auto r = Result<int>::err(ErrorCode::NET_TIMEOUT);
    EXPECT_EQ(r.error(), ErrorCode::NET_TIMEOUT);
}

TEST(ResultT, ErrorAccessOnOkReturnsInternal) {
    auto r = Result<int>::ok(0);
    EXPECT_EQ(r.error(), ErrorCode::INTERNAL);
}

TEST(ResultT, ValueOrReturnsFallbackOnErr) {
    auto r = Result<int>::err(ErrorCode::UNKNOWN);
    EXPECT_EQ(r.value_or(99), 99);
}

TEST(ResultT, ValueOrReturnsValueOnOk) {
    auto r = Result<int>::ok(7);
    EXPECT_EQ(r.value_or(99), 7);
}

// ---------- 单子操作 ----------

TEST(ResultT, MapTransformsOk) {
    auto r = Result<int>::ok(3).map([](int x) { return x * 2; });
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 6);
}

TEST(ResultT, MapPreservesErr) {
    auto r = Result<int>::err(ErrorCode::RES_DISK_FULL).map([](int x) { return x * 2; });
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::RES_DISK_FULL);
}

TEST(ResultT, AndThenChainsOk) {
    auto r = Result<int>::ok(5).and_then([](int x) -> Result<int> {
        if (x > 0) return Result<int>::ok(x + 100);
        return Result<int>::err(ErrorCode::INVALID_ARG);
    });
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 105);
}

TEST(ResultT, AndThenShortCircuitsOnErr) {
    auto r = Result<int>::err(ErrorCode::CONFIG_PARSE_FAILED)
                 .and_then([](int) -> Result<int> {
                     ADD_FAILURE() << "fn 应不被调用";
                     return Result<int>::ok(0);
                 });
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::CONFIG_PARSE_FAILED);
}

}  // namespace