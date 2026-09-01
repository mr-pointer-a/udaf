// tests/fault_injection/libudaf_fi.c
// 故障注入框架：LD_PRELOAD 共享库，通过 dlsym(RTLD_NEXT) 拦截系统调用，
// 模拟网络抖动、磁盘故障、时钟偏移、慢调用等异常环境。
//
// 用法：
//   cc -shared -fPIC -o libudaf_fi.so libudaf_fi.c -ldl -lpthread
//   LD_PRELOAD=./libudaf_fi.so UDAF_FI_NET_FAIL="connect:ECONNREFUSED:50pct" \
//       ./my_under_test_binary
//
// 约束：
//   - 仅实现 Linux 平台（依赖 <sys/socket.h> / <dlfcn.h> / dlsym）。
//   - 多线程安全：所有计数器与 RNG 用 __atomic 内建。
//   - 单元测试通过 fork+exec + LD_PRELOAD 子进程验证。

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "udaf_fi.h"

// ===========================================================================
// 工具：伪随机（xorshift64）、字符串解析
// ===========================================================================

static _Atomic uint64_t g_seed = 0;

static uint64_t xorshift64(uint64_t* s) {
    uint64_t x = *s;
    if (x == 0) x = 0x9E3779B97F4A7C15ULL;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static int match_ratio(const char* ratio_spec) {
    if (!ratio_spec) return 0;
    int pct = atoi(ratio_spec);
    if (pct <= 0) return 0;
    if (pct >= 100) return 1;
    uint64_t s = atomic_load(&g_seed);
    uint64_t r = xorshift64(&s);
    atomic_store(&g_seed, s);
    return ((int)(r % 100u) < pct);
}

static int parse_errno_token(const char* tok) {
    if (!tok) return -1;
    if (strcmp(tok, "ECONNREFUSED") == 0) return ECONNREFUSED;
    if (strcmp(tok, "ETIMEDOUT") == 0) return ETIMEDOUT;
    if (strcmp(tok, "ENETUNREACH") == 0) return ENETUNREACH;
    if (strcmp(tok, "EHOSTUNREACH") == 0) return EHOSTUNREACH;
    if (strcmp(tok, "ECONNRESET") == 0) return ECONNRESET;
    if (strcmp(tok, "EPIPE") == 0) return EPIPE;
    if (strcmp(tok, "EAGAIN") == 0) return EAGAIN;
    if (strcmp(tok, "EWOULDBLOCK") == 0) return EWOULDBLOCK;
    if (strcmp(tok, "EIO") == 0) return EIO;
    if (strcmp(tok, "EACCES") == 0) return EACCES;
    if (strcmp(tok, "EFAULT") == 0) return EFAULT;
    if (strcmp(tok, "ENOSPC") == 0) return ENOSPC;
    if (strcmp(tok, "ENOMEM") == 0) return ENOMEM;
    return -1;
}

// 全局规则（解析自环境变量，启动时一次性初始化）
typedef struct {
    char name[32];
    int  err;          // -1 表示未设置失败注入
    char ratio[16];    // "" / "10pct" / "100pct"
} fail_rule_t;

#define MAX_FAIL_RULES 16
static fail_rule_t g_net_fail[MAX_FAIL_RULES];
static size_t      g_net_fail_n = 0;
static fail_rule_t g_fs_fail[MAX_FAIL_RULES];
static size_t      g_fs_fail_n = 0;

typedef struct {
    char name[32];
    int  min_us;
    int  max_us;
} delay_rule_t;

#define MAX_DELAY_RULES 16
static delay_rule_t g_net_delay[MAX_DELAY_RULES];
static size_t       g_net_delay_n = 0;

static _Atomic int64_t g_time_skip_ns = 0;
static int             g_log_enabled = 0;

static void log_event(const char* fmt, ...) {
    if (!g_log_enabled) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[udaf_fi] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/// 应用失败规则：若命中规则则设置 errno 并返回 -1。
/// @param syscall 调用名称（用于匹配）
/// @return 1 = 命中（已设置 errno，调用方应返回 -1）；0 = 未命中
static int apply_fail(const fail_rule_t* rules, size_t n, const char* syscall) {
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(rules[i].name, syscall) != 0) continue;
        if (!match_ratio(rules[i].ratio)) continue;
        errno = rules[i].err;
        log_event("FAIL %s → errno=%d", syscall, rules[i].err);
        return 1;
    }
    return 0;
}

/// 应用延迟规则：若命中规则则 usleep 随机时长。
static void apply_delay(const delay_rule_t* rules, size_t n, const char* syscall) {
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(rules[i].name, syscall) != 0) continue;
        int span = rules[i].max_us - rules[i].min_us;
        if (span < 0) span = 0;
        uint64_t s = atomic_load(&g_seed);
        uint64_t r = xorshift64(&s);
        atomic_store(&g_seed, s);
        int us = rules[i].min_us + (span > 0 ? (int)(r % (uint64_t)span) : 0);
        if (us > 0) {
            log_event("DELAY %s → %d us", syscall, us);
            usleep((useconds_t)us);
        }
        return;
    }
}

// ===========================================================================
// 环境变量解析
// ===========================================================================

static void init_seeds(void) {
    const char* seed_env = getenv("UDAF_FI_SEED");
    if (seed_env && *seed_env) {
        uint64_t v = strtoull(seed_env, NULL, 10);
        if (v != 0) atomic_store(&g_seed, v);
    }
    if (atomic_load(&g_seed) == 0) {
        // 默认：基于时间 + pid 派生一个非零种子
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        atomic_store(&g_seed,
                     (uint64_t)ts.tv_nsec ^ ((uint64_t)getpid() << 32) ^ 0xA5A5A5A5ULL);
    }
}

static void parse_fail_env(const char* env_name,
                           fail_rule_t* out, size_t* out_n, size_t cap) {
    const char* env = getenv(env_name);
    if (!env || !*env) return;
    char buf[1024];
    strncpy(buf, env, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* save = NULL;
    char* tok = strtok_r(buf, ",", &save);
    while (tok && *out_n < cap) {
        // 格式：name:errno[:ratio[:seed]]
        char* colon1 = strchr(tok, ':');
        if (!colon1) { tok = strtok_r(NULL, ",", &save); continue; }
        *colon1 = '\0';
        char* colon2 = strchr(colon1 + 1, ':');
        if (colon2) *colon2 = '\0';

        strncpy(out[*out_n].name, tok, sizeof(out[*out_n].name) - 1);
        out[*out_n].name[sizeof(out[*out_n].name) - 1] = '\0';
        int e = parse_errno_token(colon1 + 1);
        if (e < 0) { tok = strtok_r(NULL, ",", &save); continue; }
        out[*out_n].err = e;
        if (colon2) {
            strncpy(out[*out_n].ratio, colon2 + 1,
                    sizeof(out[*out_n].ratio) - 1);
            out[*out_n].ratio[sizeof(out[*out_n].ratio) - 1] = '\0';
        } else {
            strcpy(out[*out_n].ratio, "100pct");
        }
        (*out_n)++;
        tok = strtok_r(NULL, ",", &save);
    }
}

static void parse_delay_env(const char* env_name,
                            delay_rule_t* out, size_t* out_n, size_t cap) {
    const char* env = getenv(env_name);
    if (!env || !*env) return;
    char buf[1024];
    strncpy(buf, env, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* save = NULL;
    char* tok = strtok_r(buf, ",", &save);
    while (tok && *out_n < cap) {
        // 格式：name:min_us:max_us
        char* colon1 = strchr(tok, ':');
        if (!colon1) { tok = strtok_r(NULL, ",", &save); continue; }
        *colon1 = '\0';
        char* colon2 = strchr(colon1 + 1, ':');
        if (!colon2) { tok = strtok_r(NULL, ",", &save); continue; }
        *colon2 = '\0';

        strncpy(out[*out_n].name, tok, sizeof(out[*out_n].name) - 1);
        out[*out_n].name[sizeof(out[*out_n].name) - 1] = '\0';
        out[*out_n].min_us = atoi(colon1 + 1);
        out[*out_n].max_us = atoi(colon2 + 1);
        if (out[*out_n].max_us < out[*out_n].min_us) {
            out[*out_n].max_us = out[*out_n].min_us;
        }
        (*out_n)++;
        tok = strtok_r(NULL, ",", &save);
    }
}

static void parse_time_skip(void) {
    const char* env = getenv("UDAF_FI_TIME_SKIP_US");
    if (!env || !*env) return;
    long us = atol(env);
    if (us > 0) atomic_store(&g_time_skip_ns, (int64_t)us * 1000LL);
}

// GCC/Clang constructor：库加载时执行一次
__attribute__((constructor))
static void udaf_fi_init(void) {
    init_seeds();
    parse_fail_env("UDAF_FI_NET_FAIL", g_net_fail, &g_net_fail_n, MAX_FAIL_RULES);
    parse_fail_env("UDAF_FI_FS_FAIL", g_fs_fail, &g_fs_fail_n, MAX_FAIL_RULES);
    parse_delay_env("UDAF_FI_NET_DELAY_US", g_net_delay, &g_net_delay_n, MAX_DELAY_RULES);
    parse_time_skip();
    if (getenv("UDAF_FI_LOG") && getenv("UDAF_FI_LOG")[0] == '1') {
        g_log_enabled = 1;
    }
    log_event("init: net_fail=%zu fs_fail=%zu net_delay=%zu time_skip_ns=%ld",
              g_net_fail_n, g_fs_fail_n, g_net_delay_n,
              (long)atomic_load(&g_time_skip_ns));
}

int udaf_fi_is_loaded(void) {
    return 1;  // 加载成功即返回非零
}

// ===========================================================================
// 网络 syscall 拦截
// ===========================================================================

typedef int (*socket_fn_t)(int, int, int);
typedef int (*connect_fn_t)(int, const struct sockaddr*, socklen_t);
typedef int (*accept_fn_t)(int, struct sockaddr*, socklen_t*);
typedef int (*accept4_fn_t)(int, struct sockaddr*, socklen_t*, int);
typedef int (*bind_fn_t)(int, const struct sockaddr*, socklen_t);
typedef int (*listen_fn_t)(int, int);
typedef ssize_t (*send_fn_t)(int, const void*, size_t, int);
typedef ssize_t (*recv_fn_t)(int, void*, size_t, int);
typedef ssize_t (*sendto_fn_t)(int, const void*, size_t, int,
                               const struct sockaddr*, socklen_t);
typedef ssize_t (*recvfrom_fn_t)(int, void*, size_t, int,
                                 struct sockaddr*, socklen_t*);

int socket(int domain, int type, int protocol) {
    static socket_fn_t real = NULL;
    if (!real) real = (socket_fn_t)dlsym(RTLD_NEXT, "socket");
    if (apply_fail(g_net_fail, g_net_fail_n, "socket")) return -1;
    return real(domain, type, protocol);
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    static connect_fn_t real = NULL;
    if (!real) real = (connect_fn_t)dlsym(RTLD_NEXT, "connect");
    apply_delay(g_net_delay, g_net_delay_n, "connect");
    if (apply_fail(g_net_fail, g_net_fail_n, "connect")) return -1;
    return real(sockfd, addr, addrlen);
}

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    static accept_fn_t real = NULL;
    if (!real) real = (accept_fn_t)dlsym(RTLD_NEXT, "accept");
    apply_delay(g_net_delay, g_net_delay_n, "accept");
    if (apply_fail(g_net_fail, g_net_fail_n, "accept")) return -1;
    return real(sockfd, addr, addrlen);
}

int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags) {
    static accept4_fn_t real = NULL;
    if (!real) real = (accept4_fn_t)dlsym(RTLD_NEXT, "accept4");
    apply_delay(g_net_delay, g_net_delay_n, "accept");
    if (apply_fail(g_net_fail, g_net_fail_n, "accept")) return -1;
    return real(sockfd, addr, addrlen, flags);
}

int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    static bind_fn_t real = NULL;
    if (!real) real = (bind_fn_t)dlsym(RTLD_NEXT, "bind");
    if (apply_fail(g_net_fail, g_net_fail_n, "bind")) return -1;
    return real(sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog) {
    static listen_fn_t real = NULL;
    if (!real) real = (listen_fn_t)dlsym(RTLD_NEXT, "listen");
    if (apply_fail(g_net_fail, g_net_fail_n, "listen")) return -1;
    return real(sockfd, backlog);
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    static send_fn_t real = NULL;
    if (!real) real = (send_fn_t)dlsym(RTLD_NEXT, "send");
    apply_delay(g_net_delay, g_net_delay_n, "send");
    if (apply_fail(g_net_fail, g_net_fail_n, "send")) return -1;
    return real(sockfd, buf, len, flags);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    static recv_fn_t real = NULL;
    if (!real) real = (recv_fn_t)dlsym(RTLD_NEXT, "recv");
    apply_delay(g_net_delay, g_net_delay_n, "recv");
    if (apply_fail(g_net_fail, g_net_fail_n, "recv")) return -1;
    return real(sockfd, buf, len, flags);
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen) {
    static sendto_fn_t real = NULL;
    if (!real) real = (sendto_fn_t)dlsym(RTLD_NEXT, "sendto");
    apply_delay(g_net_delay, g_net_delay_n, "send");
    if (apply_fail(g_net_fail, g_net_fail_n, "send")) return -1;
    return real(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen) {
    static recvfrom_fn_t real = NULL;
    if (!real) real = (recvfrom_fn_t)dlsym(RTLD_NEXT, "recvfrom");
    apply_delay(g_net_delay, g_net_delay_n, "recv");
    if (apply_fail(g_net_fail, g_net_fail_n, "recv")) return -1;
    return real(sockfd, buf, len, flags, src_addr, addrlen);
}

// ===========================================================================
// 文件系统 syscall 拦截
// ===========================================================================

typedef int (*open_fn_t)(const char*, int, ...);
typedef int (*close_fn_t)(int);
typedef ssize_t (*read_fn_t)(int, void*, size_t);
typedef ssize_t (*write_fn_t)(int, const void*, size_t);
typedef ssize_t (*pread_fn_t)(int, void*, size_t, off_t);
typedef ssize_t (*pwrite_fn_t)(int, const void*, size_t, off_t);
typedef int (*fsync_fn_t)(int);
typedef int (*unlink_fn_t)(const char*);
typedef off_t (*lseek_fn_t)(int, off_t, int);
typedef int (*ftruncate_fn_t)(int, off_t);

/// 判定 pathname 是否属于测试框架/运行时基础设施（不应被注入）
/// 命中模式：覆盖率数据 (.gcda)、覆盖率静态 (.gcno)、动态链接器路径、
/// 共享库 (.so)、本框架的临时文件等。
static int is_infra_path(const char* pathname) {
    if (!pathname) return 0;
    const char* dot = strrchr(pathname, '.');
    if (dot) {
        if (strcmp(dot, ".gcda") == 0) return 1;
        if (strcmp(dot, ".gcno") == 0) return 1;
    }
    /* ld.so / libc / libpthread 路径 */
    if (strstr(pathname, "/ld-linux")) return 1;
    if (strstr(pathname, "/ld.so")) return 1;
    if (strstr(pathname, "/libc.so")) return 1;
    if (strstr(pathname, "/libpthread")) return 1;
    if (strstr(pathname, "/libstdc++")) return 1;
    if (strstr(pathname, "/libgcc_s")) return 1;
    if (strstr(pathname, "/libm.so")) return 1;
    /* GCC libgcov 临时与 lock 文件 */
    if (strstr(pathname, "/tmp/gcov-")) return 1;
    return 0;
}

int open(const char* pathname, int flags, ...) {
    static open_fn_t real = NULL;
    if (!real) real = (open_fn_t)dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    /* 基础设施路径放行（gcov 写覆盖率数据、运行时加载共享库） */
    if (is_infra_path(pathname)) return real(pathname, flags, mode);
    if (apply_fail(g_fs_fail, g_fs_fail_n, "open")) return -1;
    return real(pathname, flags, mode);
}

int close(int fd) {
    static close_fn_t real = NULL;
    if (!real) real = (close_fn_t)dlsym(RTLD_NEXT, "close");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "close")) return -1;
    return real(fd);
}

ssize_t read(int fd, void* buf, size_t count) {
    static read_fn_t real = NULL;
    if (!real) real = (read_fn_t)dlsym(RTLD_NEXT, "read");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "read")) return -1;
    return real(fd, buf, count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    static write_fn_t real = NULL;
    if (!real) real = (write_fn_t)dlsym(RTLD_NEXT, "write");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "write")) return -1;
    return real(fd, buf, count);
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    static pread_fn_t real = NULL;
    if (!real) real = (pread_fn_t)dlsym(RTLD_NEXT, "pread");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "pread")) return -1;
    return real(fd, buf, count, offset);
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    static pwrite_fn_t real = NULL;
    if (!real) real = (pwrite_fn_t)dlsym(RTLD_NEXT, "pwrite");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "pwrite")) return -1;
    return real(fd, buf, count, offset);
}

int fsync(int fd) {
    static fsync_fn_t real = NULL;
    if (!real) real = (fsync_fn_t)dlsym(RTLD_NEXT, "fsync");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "fsync")) return -1;
    return real(fd);
}

int unlink(const char* pathname) {
    static unlink_fn_t real = NULL;
    if (!real) real = (unlink_fn_t)dlsym(RTLD_NEXT, "unlink");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "unlink")) return -1;
    return real(pathname);
}

off_t lseek(int fd, off_t offset, int whence) {
    static lseek_fn_t real = NULL;
    if (!real) real = (lseek_fn_t)dlsym(RTLD_NEXT, "lseek");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "lseek")) return (off_t)-1;
    return real(fd, offset, whence);
}

int ftruncate(int fd, off_t length) {
    static ftruncate_fn_t real = NULL;
    if (!real) real = (ftruncate_fn_t)dlsym(RTLD_NEXT, "ftruncate");
    if (apply_fail(g_fs_fail, g_fs_fail_n, "ftruncate")) return -1;
    return real(fd, length);
}

// ===========================================================================
// 时钟拦截（用于模拟时间跳变）
// ===========================================================================

int clock_gettime(clockid_t clk_id, struct timespec* tp) {
    static int (*real)(clockid_t, struct timespec*) = NULL;
    if (!real) real = (int (*)(clockid_t, struct timespec*))dlsym(RTLD_NEXT, "clock_gettime");
    int r = real(clk_id, tp);
    if (r == 0 && tp != NULL) {
        int64_t skip = atomic_load(&g_time_skip_ns);
        if (skip != 0) {
            tp->tv_sec += skip / 1000000000LL;
            tp->tv_nsec += skip % 1000000000LL;
            if (tp->tv_nsec >= 1000000000L) {
                tp->tv_sec += 1;
                tp->tv_nsec -= 1000000000L;
            }
        }
    }
    return r;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    static int (*real)(const struct timespec*, struct timespec*) = NULL;
    if (!real) real = (int (*)(const struct timespec*, struct timespec*))dlsym(RTLD_NEXT, "nanosleep");
    int64_t skip = atomic_load(&g_time_skip_ns);
    if (skip != 0 && req != NULL) {
        struct timespec adjusted = *req;
        adjusted.tv_sec += skip / 1000000000LL;
        adjusted.tv_nsec += skip % 1000000000LL;
        if (adjusted.tv_nsec >= 1000000000L) {
            adjusted.tv_sec += 1;
            adjusted.tv_nsec -= 1000000000L;
        }
        return real(&adjusted, rem);
    }
    return real(req, rem);
}
