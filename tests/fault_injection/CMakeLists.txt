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
# 关键：libudaf_fi.so 不编译/链接 ASAN。它通过 LD_PRELOAD 加载到被测进程中，
# 必须先于被测进程的 ASAN runtime 被 ld.so 加载，否则 ASAN runtime 会报
# "does not come first in initial library list" 错误（无法拦截 syscall）。
# 通过 target_compile_options / target_link_options 显式覆盖全局 ASAN 选项。
target_compile_options(udaf_fi PRIVATE -fno-sanitize=address -fno-sanitize=undefined)
target_link_options(udaf_fi PRIVATE -fno-sanitize=address -fno-sanitize=undefined)

# 所有产物输出到 build_dir/tests/fault_injection/，方便 LD_PRELOAD 时按相对路径找
foreach(_name udaf_fi)
    set_target_properties(${_name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

# 自检测试（GTest 父进程）+ 5 个 helper 子进程
# 注意：test_fault_injection 不直接链接 udaf_fi（共享库）。
# udaf_fi 的 __attribute__((constructor)) 在 dlopen 时执行 vfprintf/dlsym，
# 与 ASAN 的 interceptor 在静态初始化阶段存在冲突（baseline 就有此问题）。
# 我们仅在子进程通过 LD_PRELOAD 加载 udaf_fi，父进程不需要链接它。
add_executable(test_fault_injection test_fault_injection.cpp)
target_link_libraries(test_fault_injection PRIVATE GTest::gtest GTest::gtest_main)

# 每个 helper 是一个独立可执行文件（同名不同 basename），共享同一份源
foreach(helper fi_helper_socket fi_helper_socket_repeat fi_helper_connect_delay fi_helper_fs fi_helper_clock_skip)
    add_executable(${helper} fi_helpers.cpp)
    target_compile_options(${helper} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${helper} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        OUTPUT_NAME "${helper}"
    )
endforeach()

# Wal 错误路径 helper（独立源，需链接 udaf_platform_fs + udaf_core）
foreach(helper fi_helper_wal_open_fail fi_helper_wal_write_fail fi_helper_wal_fsync_fail fi_helper_wal_write_eintr fi_helper_wal_truncate_lseek_fail fi_helper_wal_truncate_all_ftruncate_fail fi_helper_wal_read_eintr fi_helper_wal_replay_seek_fail fi_helper_wal_replay_stream_seek_fail fi_helper_wal_truncate_rewrite_seek_fail fi_helper_wal_truncate_rewrite_ftruncate_fail fi_helper_wal_truncate_rewrite_write_fail fi_helper_wal_truncate_kept_empty_ftruncate_fail fi_helper_wal_truncate_all_seek_fail)
    add_executable(${helper} fi_helpers_wal.cpp)
    target_compile_options(${helper} PRIVATE -Wall -Wextra -Wpedantic)
    # 通过链接 udaf_platform_fs 让 helper 拿到 wal.cpp 实现（同一份 wal.cpp.o 被
    # 所有 helper 共享，gcov 会在 udaf_platform_fs.dir/wal.cpp.gcda 里累加所有
    # helper 的覆盖数据；lcov 收集时按源文件路径合并，最终行覆盖率 = WalTest
    # 单测 + 所有 FI helper 贡献的并集）。
    target_link_libraries(${helper} PRIVATE udaf_platform_fs)
    target_include_directories(${helper} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    set_target_properties(${helper} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fault_injection"
        OUTPUT_NAME "${helper}"
    )
endforeach()

add_test(NAME test_fault_injection COMMAND test_fault_injection)
set_tests_properties(test_fault_injection PROPERTIES
    LABELS "fault_injection"
    # 关键：在 ASan 构建下，libudaf_fi.so 通过 LD_PRELOAD 加载到被测进程，
    # 会先于 libasan.so 进入地址空间，触发 ASan 自身的 link-order 校验并报错
    # "ASan runtime does not come first"。该选项关闭校验，保证 FI 测试可运行。
    ENVIRONMENT "ASAN_OPTIONS=verify_asan_link_order=0"
)
