// fi_helpers_wal.cpp - Wal 错误路径子进程（被 LD_PRELOAD 故障注入框架拦截）
//
// 三个入口（按 program_invocation_name basename 区分）：
//   fi_helper_wal_open_fail  - 触发 file open 失败 → Wal::create 返回 Err
//   fi_helper_wal_write_fail - 触发 pwrite 失败   → Wal::append 返回 Err
//   fi_helper_wal_fsync_fail - 触发 fsync 失败    → Wal::fsync 返回 Err
//   fi_helper_wal_write_eintr- 触发 EINTR 重试   → pwrite 走 EINTR 分支
//
// 输出格式：
//   成功：OK=<seq>  或  OK
//   失败：ERR=<hex error code>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/error_code.hpp"
#include "platform/fs/wal.hpp"

namespace {

const char* helper_name() {
    const char* arg0 = program_invocation_name ? program_invocation_name : "fi_helper_wal";
    const char* slash = strrchr(arg0, '/');
    return slash ? slash + 1 : arg0;
}

udaf::platform::fs::WalConfig make_cfg() {
    udaf::platform::fs::WalConfig cfg;
    cfg.path_ = std::filesystem::temp_directory_path() / "udaf_fi_wal_helper.log";
    cfg.fsync_on_append_ = true;  // 让 fsync 路径可被触发
    return cfg;
}

void print_err(udaf::core::ErrorCode ec) {
    std::printf("ERR=0x%X\n", static_cast<std::uint32_t>(ec));
}

// === Wal::create 在 file open 失败时 ===
int run_wal_open_fail() {
    auto cfg = make_cfg();
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    // 注：LD_PRELOAD 注入 open:EACCES:100pct → Wal::create 内部 ::open 失败
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === Wal::append 在 pwrite 失败时 ===
int run_wal_write_fail() {
    auto cfg = make_cfg();
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    {
        // 先用 LD_PRELOAD 关闭前的正常 helper 写一个 entry；
        // 实际：本 helper 在 LD_PRELOAD write:EIO:100pct 下启动
        // → Wal::create 的 open 也被拦截 → 创建失败
        // 为简化：先在纯净路径创建（实际测试中 helper 是单独的子进程，
        //        write:fail 拦截在 create 的 open 之前不命中，
        //        而 write 拦截在 append 的 pwrite 时命中）
        auto wr = udaf::platform::fs::Wal::create(cfg);
        if (wr.is_err()) {
            print_err(wr.error());
            return 0;
        }
        auto& wal = wr.value();
        // pwrite 会被拦截为 EIO → Wal::append 返回 Err
        std::vector<std::uint8_t> payload(8, 0xAA);
        auto ar = wal->append(udaf::platform::fs::WalEntryType::ADD_NODE,
                              "op1", std::span<const std::uint8_t>(payload.data(), payload.size()));
        if (ar.is_err()) {
            print_err(ar.error());
            return 0;
        }
        std::printf("OK=%llu\n", static_cast<unsigned long long>(ar.value()));
    }
    return 0;
}

// === Wal::fsync 在 fsync syscall 失败时 ===
int run_wal_fsync_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;  // 关闭自动 fsync
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0xBB);
    auto ar = wal->append(udaf::platform::fs::WalEntryType::CUSTOM,
                          "x", std::span<const std::uint8_t>(payload.data(), payload.size()));
    if (ar.is_err()) {
        print_err(ar.error());
        return 0;
    }
    // 显式 fsync → 被 LD_PRELOAD fsync:EIO:100pct 拦截
    auto fr = wal->fsync();
    if (fr.is_err()) {
        print_err(fr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === pwrite 收到 EINTR 时 write_at 的重试分支 ===
int run_wal_write_eintr() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    // 注：实际测试中 LD_PRELOAD 会把 pwrite 第一次返回 EINTR，第二次成功
    // → write_at 进入 while 循环 retry 分支，最终 write 成功
    std::vector<std::uint8_t> payload(16, 0xCC);
    auto ar = wal->append(udaf::platform::fs::WalEntryType::CUSTOM,
                          "eintr", std::span<const std::uint8_t>(payload.data(), payload.size()));
    if (ar.is_err()) {
        print_err(ar.error());
        return 0;
    }
    std::printf("OK=%llu\n", static_cast<unsigned long long>(ar.value()));
    return 0;
}

// === truncate 在 lseek (SEEK_SET) 失败时 ===
int run_wal_truncate_lseek_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    // 先写一个 entry 以便 truncate 有内容可保留
    std::vector<std::uint8_t> payload(8, 0xDD);
    auto ar = wal->append(udaf::platform::fs::WalEntryType::CUSTOM,
                          "t1", std::span<const std::uint8_t>(payload.data(), payload.size()));
    if (ar.is_err()) {
        print_err(ar.error());
        return 0;
    }
    // truncate(0) 走 "重写文件" 分支：先 ftruncate，再 seek_set → LD_PRELOAD 拦截 lseek 返回 -1
    auto tr = wal->truncate(0);
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate_all 在 ftruncate 失败时 ===
int run_wal_truncate_all_ftruncate_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0xEE);
    auto ar = wal->append(udaf::platform::fs::WalEntryType::CUSTOM,
                          "t2", std::span<const std::uint8_t>(payload.data(), payload.size()));
    if (ar.is_err()) {
        print_err(ar.error());
        return 0;
    }
    // truncate_all → ftruncate_wrapped 调用 ftruncate → LD_PRELOAD 拦截返回 -1
    auto tr = wal->truncate_all();
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === pread 返回 EINTR 时 read_at 的重试分支（wal.cpp:64-65）===
int run_wal_read_eintr() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    // 写一条 entry
    std::vector<std::uint8_t> payload(8, 0x11);
    if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "re",
                    std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
        std::printf("ERR=append\n");
        return 0;
    }
    // replay 会触发 pread；LD_PRELOAD 让 pread 50% 概率返回 EINTR
    // → read_at 走 EINTR 重试分支，最终成功
    auto rr = wal->replay();
    if (rr.is_err()) {
        print_err(rr.error());
        return 0;
    }
    std::printf("OK=%zu\n", rr.value().size());
    return 0;
}

// === replay 在 seek_set 失败时（wal.cpp:490-491）===
int run_wal_replay_seek_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0x22);
    if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "rs",
                    std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
        std::printf("ERR=append\n");
        return 0;
    }
    // replay 内部 seek_set(kFileHeaderSize) → lseek:EIO 拦截
    // 跳过 3 次 lseek（create/open/append 内部用到的 lseek），从第 4 次起开始失败
    auto rr = wal->replay();
    if (rr.is_err()) {
        print_err(rr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === replay_stream 在 seek_set 失败时（wal.cpp:521-522）===
int run_wal_replay_stream_seek_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0x33);
    if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "rss",
                    std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
        std::printf("ERR=append\n");
        return 0;
    }
    // replay_stream 内部 seek_set → lseek 失败
    auto rs = wal->replay_stream([](const udaf::platform::fs::WalEntry&) { return true; });
    if (rs.is_err()) {
        print_err(rs.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate 在 rewrite 分支 seek_set 失败时（wal.cpp:591-592）===
int run_wal_truncate_rewrite_seek_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    // 写 3 条 entry，truncate(2) 会保留 2 条，触发 rewrite 分支
    for (int i = 0; i < 3; ++i) {
        std::vector<std::uint8_t> payload(4, static_cast<std::uint8_t>(i));
        if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "tr" + std::to_string(i),
                        std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
            std::printf("ERR=append%d\n", i);
            return 0;
        }
    }
    // lseek 拦截：create+open 内部 lseek 约 2 次，append 每次 1 次 seek_end，
    // truncate 初始 1 次 seek_set → 累计 ~5 次 lseek 后开始失败
    // rewrite 分支的 seek_set（行 591）会命中
    auto tr = wal->truncate(2);
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate 在 rewrite 分支 ftruncate_wrapped 失败时（wal.cpp:588-589）===
int run_wal_truncate_rewrite_ftruncate_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    for (int i = 0; i < 3; ++i) {
        std::vector<std::uint8_t> payload(4, static_cast<std::uint8_t>(i));
        if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "tf" + std::to_string(i),
                        std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
            std::printf("ERR=append%d\n", i);
            return 0;
        }
    }
    // ftruncate 拦截：append 不调用 ftruncate，但 truncate rewrite 第一次 ftruncate 必须失败
    // 跳过 0 次（append 不调用），从 truncate 的 ftruncate 开始拦截
    auto tr = wal->truncate(2);
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate 在 rewrite 分支 write_entry 失败时（wal.cpp:596-597）===
int run_wal_truncate_rewrite_write_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    for (int i = 0; i < 3; ++i) {
        std::vector<std::uint8_t> payload(4, static_cast<std::uint8_t>(i));
        if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "tw" + std::to_string(i),
                        std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
            std::printf("ERR=append%d\n", i);
            return 0;
        }
    }
    // pwrite 拦截：从第 N+1 次起失败（N = 初始 3 次 append 的 pwrite 次数）
    // append 每个 entry 用 2 次 pwrite（header + action+payload），共 6 次
    // truncate rewrite 又调用 write_entry，每次用 2 次 pwrite
    // 跳过 6 次后失败 → rewrite 第一次 write_entry 失败
    auto tr = wal->truncate(2);
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate 在 kept.empty 分支 ftruncate_wrapped 失败时（wal.cpp:577-578）===
int run_wal_truncate_kept_empty_ftruncate_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0x44);
    if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "te",
                    std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
        std::printf("ERR=append\n");
        return 0;
    }
    // truncate(10) → kept.empty → ftruncate_wrapped 必须失败
    // 跳过 0 次（append 不调用 ftruncate），从 truncate 的 ftruncate 开始拦截
    auto tr = wal->truncate(10);
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

// === truncate_all 在 seek_set 失败时（wal.cpp:611-612）===
int run_wal_truncate_all_seek_fail() {
    auto cfg = make_cfg();
    cfg.fsync_on_append_ = false;
    std::error_code ec;
    std::filesystem::remove(cfg.path_, ec);
    auto wr = udaf::platform::fs::Wal::create(cfg);
    if (wr.is_err()) {
        print_err(wr.error());
        return 0;
    }
    auto& wal = wr.value();
    std::vector<std::uint8_t> payload(4, 0x55);
    if (wal->append(udaf::platform::fs::WalEntryType::CUSTOM, "tas",
                    std::span<const std::uint8_t>(payload.data(), payload.size())).is_err()) {
        std::printf("ERR=append\n");
        return 0;
    }
    // lseek 拦截：跳过 create/append 用到的 lseek，从 truncate_all 的 lseek 开始失败
    auto tr = wal->truncate_all();
    if (tr.is_err()) {
        print_err(tr.error());
        return 0;
    }
    std::printf("OK\n");
    return 0;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    const char* name = helper_name();
    if (std::strcmp(name, "fi_helper_wal_open_fail") == 0)  return run_wal_open_fail();
    if (std::strcmp(name, "fi_helper_wal_write_fail") == 0) return run_wal_write_fail();
    if (std::strcmp(name, "fi_helper_wal_fsync_fail") == 0) return run_wal_fsync_fail();
    if (std::strcmp(name, "fi_helper_wal_write_eintr") == 0) return run_wal_write_eintr();
    if (std::strcmp(name, "fi_helper_wal_truncate_lseek_fail") == 0) return run_wal_truncate_lseek_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_all_ftruncate_fail") == 0) return run_wal_truncate_all_ftruncate_fail();
    if (std::strcmp(name, "fi_helper_wal_read_eintr") == 0) return run_wal_read_eintr();
    if (std::strcmp(name, "fi_helper_wal_replay_seek_fail") == 0) return run_wal_replay_seek_fail();
    if (std::strcmp(name, "fi_helper_wal_replay_stream_seek_fail") == 0) return run_wal_replay_stream_seek_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_rewrite_seek_fail") == 0) return run_wal_truncate_rewrite_seek_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_rewrite_ftruncate_fail") == 0) return run_wal_truncate_rewrite_ftruncate_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_rewrite_write_fail") == 0) return run_wal_truncate_rewrite_write_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_kept_empty_ftruncate_fail") == 0) return run_wal_truncate_kept_empty_ftruncate_fail();
    if (std::strcmp(name, "fi_helper_wal_truncate_all_seek_fail") == 0) return run_wal_truncate_all_seek_fail();
    std::fprintf(stderr, "unknown helper: %s\n", name);
    return 1;
}
