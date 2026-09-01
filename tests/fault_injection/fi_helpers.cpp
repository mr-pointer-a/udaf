// tests/fault_injection/fi_helpers.cpp
// 故障注入测试辅助子进程集合。
// 每个 helper 通过 argv[0] 选择执行的功能，输出形如 "OK" / "ERR=ECONNREFUSED" /
// "OK=42 ERR=58" / "ELAPSED_MS=35" / "DELTA_MS=102" 等可被父进程解析的格式。

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {

const char* helper_name() {
    // argv[0] 通常是绝对路径，取 basename
    const char* arg0 = program_invocation_name ? program_invocation_name : "fi_helper";
    const char* slash = strrchr(arg0, '/');
    return slash ? slash + 1 : arg0;
}

const char* errno_name(int e) {
    // EAGAIN == EWOULDBLOCK on Linux, use fold case.
    switch (e) {
        case ECONNREFUSED: return "ECONNREFUSED";
        case ETIMEDOUT:    return "ETIMEDOUT";
        case ENETUNREACH:  return "ENETUNREACH";
        case EHOSTUNREACH: return "EHOSTUNREACH";
        case ECONNRESET:   return "ECONNRESET";
        case EPIPE:        return "EPIPE";
        case EAGAIN:       return "EAGAIN";
        case EIO:          return "EIO";
        case EACCES:       return "EACCES";
        case EFAULT:       return "EFAULT";
        case ENOSPC:       return "ENOSPC";
        case ENOMEM:       return "ENOMEM";
        default:           return "EUNKNOWN";
    }
}

int run_socket_main(void) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("ERR=%s\n", errno_name(errno));
        return 0;
    }
    ::close(fd);
    printf("OK\n");
    return 0;
}

int run_socket_repeat_main(void) {
    int ok = 0;
    int err = 0;
    for (int i = 0; i < 100; ++i) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) ++err;
        else { ++ok; ::close(fd); }
    }
    printf("OK=%d ERR=%d\n", ok, err);
    return 0;
}

int run_connect_delay_main(void) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("SOCKET_ERR=%s\n", errno_name(errno));
        return 0;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1);  // 不存在的端口
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    auto t0 = std::chrono::steady_clock::now();
    // 计时从 connect 开始；不论 connect 成功或失败，FI 拦截点都计入了延时
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    printf("ELAPSED_MS=%ld RC=%d ERR=%s\n", static_cast<long>(ms), rc,
           rc < 0 ? errno_name(errno) : "none");
    ::close(fd);
    return 0;
}

int run_fs_open_main(void) {
    int fd = ::open("/tmp/udaf_fi_test_should_fail", O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("ERR=%s\n", errno_name(errno));
        return 0;
    }
    ::close(fd);
    ::unlink("/tmp/udaf_fi_test_should_fail");
    printf("OK\n");
    return 0;
}

int run_clock_skip_main(void) {
    timespec t0{};
    ::clock_gettime(CLOCK_MONOTONIC, &t0);
    // 真实 sleep 100ms（不被 FI 加速）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    timespec t1{};
    ::clock_gettime(CLOCK_MONOTONIC, &t1);
    long delta_ms = (t1.tv_sec - t0.tv_sec) * 1000L
                  + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    printf("DELTA_MS=%ld\n", delta_ms);
    return 0;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    const char* name = helper_name();
    if (strcmp(name, "fi_helper_socket") == 0) return run_socket_main();
    if (strcmp(name, "fi_helper_socket_repeat") == 0) return run_socket_repeat_main();
    if (strcmp(name, "fi_helper_connect_delay") == 0) return run_connect_delay_main();
    if (strcmp(name, "fi_helper_fs") == 0) return run_fs_open_main();
    if (strcmp(name, "fi_helper_clock_skip") == 0) return run_clock_skip_main();
    fprintf(stderr, "unknown helper: %s\n", name);
    return 1;
}
