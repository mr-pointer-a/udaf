# tests/core 单元测试 - 阶段 A1
# 详细用例清单见 docs/05-test-plan.md §5.3.1（8 文件 / ~38 用例）
# 实施 A1 时拆分为 6 个文件 / ~46 用例

add_executable(test_core
    test_core.cpp                # smoke 锚点
    test_result_t.cpp            # Result<T> 主模板（12 用例）
    test_result_void.cpp         # Result<void> 特化（7 用例）
    test_error_code.cpp          # ErrorCode 枚举与工具（8 用例）
    test_logger.cpp              # Logger 单例门面（5 用例）
    test_config_loader.cpp       # ConfigLoader 加载与校验（6 用例）
    test_buffer.cpp              # RingBuffer / DynamicRingBuffer（8 用例）
)

target_link_libraries(test_core
    PRIVATE
        udaf::udaf_core
        GTest::gtest_main
)

target_compile_features(test_core PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(test_core
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60
)