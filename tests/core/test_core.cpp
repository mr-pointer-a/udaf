// test_core.cpp - 阶段 A1 占位
// 详细用例见 docs/05-test-plan.md §5.3.1（8 文件 / ~38 用例）
// 实施阶段 A1 时拆分为独立文件：test_result_*.cc / test_error_code_*.cc 等

#include "result.hpp"           // 占位 - 阶段 A1 实施后填充
#include "error_code.hpp"
#include <gtest/gtest.h>

// 占位：阶段 A1 实施时填充 ~38 个测试用例
// 当前仅 1 个 smoke test 确保 cmake/gtest 链路打通

TEST(UdafCore, SmokeTest) {
    EXPECT_TRUE(true) << "UDAF core 模块占位测试（阶段 A1 实施时替换为真实用例）";
}