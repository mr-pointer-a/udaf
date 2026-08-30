// test_result_void.cpp - Result<void> 特化测试（共 7 用例）
#include "result.hpp"
#include "error_code.hpp"

#include <gtest/gtest.h>

using udaf::core::ErrorCode;
using udaf::core::Result;

TEST(ResultVoid, OkIsOk) {
    auto r = Result<void>::ok();
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoid, ErrIsErr) {
    auto r = Result<void>::err(ErrorCode::NODE_INIT_FAILED);
    EXPECT_FALSE(r.is_ok());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), ErrorCode::NODE_INIT_FAILED);
}

TEST(ResultVoid, DefaultIsUninitialized) {
    Result<void> r;
    EXPECT_FALSE(r.is_ok());
    EXPECT_FALSE(r.is_err());
}

TEST(ResultVoid, OrElseRecoveryReturnsOk) {
    bool recovered = false;
    auto r = Result<void>::err(ErrorCode::CONFIG_MISSING_REQUIRED)
                 .or_else([&](ErrorCode) -> Result<void> {
                     recovered = true;
                     return Result<void>::ok();
                 });
    EXPECT_TRUE(r.is_ok());
    EXPECT_TRUE(recovered);
}

TEST(ResultVoid, OrElsePreservesOk) {
    auto r = Result<void>::ok().or_else([](ErrorCode) -> Result<void> {
        ADD_FAILURE() << "or_else 不应被调用";
        return Result<void>::err(ErrorCode::INTERNAL);
    });
    EXPECT_TRUE(r.is_ok());
}

TEST(ResultVoid, OnErrorInvokesOnErr) {
    bool invoked = false;
    auto r = Result<void>::err(ErrorCode::INTERNAL)
                 .on_error([&](ErrorCode code) {
                     invoked = true;
                     EXPECT_EQ(code, ErrorCode::INTERNAL);
                 });
    EXPECT_TRUE(r.is_err());
    EXPECT_TRUE(invoked);
}

TEST(ResultVoid, OnErrorNotInvokedOnOk) {
    bool invoked = false;
    auto r = Result<void>::ok().on_error([&](ErrorCode) { invoked = true; });
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(invoked);
}