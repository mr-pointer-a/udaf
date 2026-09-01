// tests/fault_injection/udaf_fi.h
// 故障注入框架的公共配置头（与 libudaf_fi.so 配套使用）。
//
// 启用方式：
//   LD_PRELOAD=libudaf_fi.so ./test_binary [args...]
//
// 配置（环境变量，逗号分隔规则）：
//   UDAF_FI_NET_FAIL         注入网络失败
//                            格式：<syscall>:<err>[:<ratio>[:<seed>]]
//                            示例：connect:ECONNREFUSED:50pct
//                                  send:ETIMEDOUT:10pct:42
//   UDAF_FI_NET_DELAY_US     注入网络延迟（微秒，均匀分布）
//                            格式：<syscall>:<min_us>:<max_us>
//                            示例：recv:1000:5000
//   UDAF_FI_TIME_SKIP_US     时钟跳变（单调偏移，纳秒）
//                            格式：<offset_us>
//                            示例：5000    （clock_gettime / times 全部快进 5ms）
//   UDAF_FI_FS_FAIL          文件系统失败注入
//                            格式同 UDAF_FI_NET_FAIL
//                            示例：open:EACCES:5pct
//   UDAF_FI_LOG              =1 打印每次拦截（仅排查用，默认 0）
//
// 设计原则：
//   1. 默认不拦截——只有显式设置了对应环境变量才生效，避免污染生产路径。
//   2. 通过 dlsym(RTLD_NEXT) 转发到 libc 原实现，未匹配规则的调用零开销。
//   3. 失败注入用伪随机（线程局部 xorshift），seed 可复现。
//
// 注意：本头仅做文档说明；C 程序可直接 #include 读取 env 变量并附加到子进程。

#ifndef UDAF_TESTS_FAULT_INJECTION_UDAF_FI_H_
#define UDAF_TESTS_FAULT_INJECTION_UDAF_FI_H_

#ifdef __cplusplus
extern "C" {
#endif

/// 检查故障注入库是否启用（libudaf_fi.so 是否被 LD_PRELOAD）。
/// 在测试代码中可用于断言"如果环境要求则必须加载"。
/// @return 非零 = 已加载。
int udaf_fi_is_loaded(void) __attribute__((weak));

#ifdef __cplusplus
}
#endif

#endif  // UDAF_TESTS_FAULT_INJECTION_UDAF_FI_H_
