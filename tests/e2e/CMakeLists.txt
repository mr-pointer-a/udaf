# E2E 目标已在根级 CMakeLists.txt 中定义（无条件构建）
# 本文件仅注册为 ctest 测试

include(CTest)

add_test(NAME udaf_e2e
    COMMAND $<TARGET_FILE:udaf_e2e>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(udaf_e2e PROPERTIES
    LABELS integration
    TIMEOUT 30)