# 故障注入框架（Fault Injection）

> 用于在不修改被测代码的前提下，模拟网络抖动、磁盘故障、时钟偏移等异常环境。
> 实现方式：编译为共享库 `libudaf_fi.so`，通过 `LD_PRELOAD` 在测试进程启动时加载。

## 1. 拦截的 syscall

| 类别 | syscall |
|------|---------|
| 网络 | `socket` / `connect` / `accept` / `accept4` / `bind` / `listen` / `send` / `recv` / `sendto` / `recvfrom` |
| 文件 | `open` / `close` / `read` / `write` / `fsync` / `unlink` |
| 时钟 | `clock_gettime` / `nanosleep` |

未列出的 syscall 不被拦截，零运行时开销。

## 2. 环境变量

| 变量 | 格式 | 示例 |
|------|------|------|
| `UDAF_FI_NET_FAIL` | `<syscall>:<err>[:<ratio>]` | `connect:ECONNREFUSED:50pct` |
| `UDAF_FI_FS_FAIL` | 同上 | `open:EACCES:5pct` |
| `UDAF_FI_NET_DELAY_US` | `<syscall>:<min_us>:<max_us>` | `recv:1000:5000` |
| `UDAF_FI_TIME_SKIP_US` | `<offset_us>`（单调偏移） | `5000` |
| `UDAF_FI_SEED` | `<u64>`（伪随机种子，可复现） | `42` |
| `UDAF_FI_LOG` | `1` 启用拦截日志 | - |

支持的 errno 名：`ECONNREFUSED` / `ETIMEDOUT` / `ENETUNREACH` / `EHOSTUNREACH` / `ECONNRESET` / `EPIPE` / `EAGAIN` / `EIO` / `EACCES` / `EFAULT` / `ENOSPC` / `ENOMEM`。

## 3. 使用示例

### 3.1 直接运行被测程序

```bash
LD_PRELOAD=./tests/fault_injection/libudaf_fi.so \
UDAF_FI_NET_FAIL="connect:ETIMEDOUT:30pct,send:ECONNRESET:5pct" \
UDAF_FI_NET_DELAY_US="recv:1000:5000" \
./build/my_under_test_binary
```

### 3.2 在 GTest 集成测试中派生带故障的子进程

```cpp
#include <cstdlib>
#include <sys/wait.h>

// 派生带 LD_PRELOAD 的子进程执行 my_binary
pid_t pid = fork();
if (pid == 0) {
    setenv("LD_PRELOAD", "./tests/fault_injection/libudaf_fi.so", 1);
    setenv("UDAF_FI_NET_FAIL", "connect:ECONNREFUSED:50pct", 1);
    setenv("UDAF_FI_SEED", "42", 1);
    execl("./build/my_binary", "./build/my_binary", (char*)NULL);
    _exit(127);
}
int status; waitpid(pid, &status, 0);
```

### 3.3 验证框架自检

```bash
cmake -B build -DUDAF_ENABLE_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build -L fault_injection --output-on-failure
```

## 4. 典型场景

| 场景 | 环境变量配置 | 测试目标 |
|------|------------|---------|
| 网络抖动 | `NET_DELAY_US=recv:5000:30000` | 客户端超时退避 |
| 设备断网 | `NET_FAIL=connect:ETIMEDOUT:50pct` | 服务发现重连 |
| 广播风暴 | `NET_FAIL=recv:EAGAIN:100pct` | 速率限制逻辑 |
| WAL 写失败 | `FS_FAIL=write:ENOSPC:30pct,fsync:EIO:10pct` | crash recovery |
| 时钟跳变 | `TIME_SKIP_US=60000000`（60s 跳变） | TLS 会话过期检测 |
| 心跳超时 | `TIME_SKIP_US=45000000`（45s） | 心跳聚合超时 |

## 5. 设计原则

1. **默认不拦截**：未设置任何环境变量时，框架是空操作（除 `udaf_fi_is_loaded()` 之外无副作用）。
2. **进程隔离**：拦截仅影响当前进程及其 fork 的子进程，不污染全局。
3. **可复现**：`UDAF_FI_SEED` 固定后，所有伪随机决策可重现。
4. **零侵入**：被测代码无需任何修改或链接选项。
5. **多线程安全**：所有计数器与 RNG 用 `_Atomic`，无锁。
