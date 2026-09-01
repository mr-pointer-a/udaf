# tests/fault_injection - 故障注入框架（LD_PRELOAD + dlsym）
#
# 框架提供 libudaf_fi.so 共享库，可在不修改被测代码的前提下拦截：
#   - 网络 syscall：socket / connect / accept / bind / listen / send / recv / sendto / recvfrom
#   - 文件 syscall：open / close / read / write / fsync / unlink
#   - 时钟：clock_gettime / nanosleep（用于时间跳变）
#
# 通过环境变量配置（启动时解析，无运行时开销）：
#   UDAF_FI_NET_FAIL    "connect:ECONNREFUSED:50pct,send:ETIMEDOUT:10pct"
#   UDAF_FI_NET_DELAY_US "recv:1000:5000"
#   UDAF_FI_FS_FAIL     "open:EACCES:5pct"
#   UDAF_FI_TIME_SKIP_US "5000"
#   UDAF_FI_LOG         "1"   # 启用每次拦截日志（默认关闭）
#   UDAF_FI_SEED        "42"  # 可复现伪随机
#
# 用途（CI 集成）：
#   - 网络抖动测试：UDT 重传 / 设备端断网 / 服务发现风暴抑制
#   - 文件系统故障：WAL 写失败 / 配置加载失败 / 审计日志 fsync 超时
#   - 时钟偏移：心跳超时 / TLS 会话过期 / scheduler 唤醒

add_library(udaf_fi SHARED libudaf_fi.c)
target_compile_options(udaf_fi PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_link_libraries(udaf_fi PRIVATE dl)
target_include_directories(udaf_fi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 所有产物输出到 build_dir/tests/fault_injection/，方便 LD_PRELOAD 时按相对路径找
foreach(_name udaf_fi)
    set_target_properties(${_name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

# 自检测试（GTest 父进程）+ 5 个 helper 子进程
add_executable(test_fault_injection test_fault_injection.cpp)
target_link_libraries(test_fault_injection PRIVATE udaf_fi GTest::gtest GTest::gtest_main)

# 每个 helper 是一个独立可执行文件（同名不同 basename），共享同一份源
foreach(helper fi_helper_socket fi_helper_socket_repeat fi_helper_connect_delay fi_helper_fs fi_helper_clock_skip)
    add_executable(${helper} fi_helpers.cpp)
    target_compile_options(${helper} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${helper} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        OUTPUT_NAME "${helper}"
    )
endforeach()

add_test(NAME test_fault_injection COMMAND test_fault_injection)
set_tests_properties(test_fault_injection PROPERTIES
    LABELS "fault_injection"
)
