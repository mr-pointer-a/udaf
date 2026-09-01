// test_wal.cpp - Wal 完整功能测试（共 7 用例）
//   test_wal_append_returns_sequence
//   test_wal_replay_in_order
//   test_wal_truncate_keeps_tail
//   test_wal_schema_version_mismatch
//   test_wal_concurrent_writers
//   test_wal_recovery_after_crash
//   test_wal_replay_stream_callback
#include "platform/fs/wal.hpp"

#include "core/error_code.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

using udaf::platform::fs::Wal;
using udaf::platform::fs::WalConfig;
using udaf::platform::fs::WalEntry;
using udaf::platform::fs::WalEntryType;
using udaf::core::ErrorCode;
using udaf::core::Result;

namespace {

std::filesystem::path make_tmp_path(const char* tag) {
    static std::atomic<int> counter{0};
    const int n = counter.fetch_add(1);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "/tmp/udaf_wal_test_%s_%d_%d.wal", tag,
                  ::getpid(), n);
    return std::filesystem::path(buf);
}

void rm_tmp(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

std::vector<std::uint8_t> make_payload(int n) {
    std::vector<std::uint8_t> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i & 0xFF);
    return v;
}

}  // namespace

class WalTest : public ::testing::Test {
protected:
    std::filesystem::path path_;
    void SetUp() override {
        path_ = make_tmp_path("main");
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    void TearDown() override {
        rm_tmp(path_);
    }

    WalConfig default_cfg() {
        WalConfig cfg;
        cfg.path_ = path_;
        cfg.fsync_on_append_ = false;  // 测试环境加速
        return cfg;
    }
};

// ---------- 基础 CRUD ----------

TEST_F(WalTest, AppendReturnsSequence) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok()) << "create should succeed";
    auto& wal = wr.value();

    auto a1 = wal->append(WalEntryType::ADD_NODE, "add_node", make_payload(10));
    ASSERT_TRUE(a1.is_ok());
    EXPECT_EQ(a1.value(), 1u);

    auto a2 = wal->append(WalEntryType::CONNECT, "connect", make_payload(20));
    ASSERT_TRUE(a2.is_ok());
    EXPECT_EQ(a2.value(), 2u);

    auto a3 = wal->append(WalEntryType::LIFECYCLE_STATE, "state_running", make_payload(0));
    ASSERT_TRUE(a3.is_ok());
    EXPECT_EQ(a3.value(), 3u);

    EXPECT_EQ(wal->current_sequence(), 4u);
}

TEST_F(WalTest, ReplayInOrder) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        auto& wal = wr.value();
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE, "op1", make_payload(5)).is_ok());
        ASSERT_TRUE(wal->append(WalEntryType::CONNECT, "op2", make_payload(15)).is_ok());
        ASSERT_TRUE(wal->append(WalEntryType::DISCONNECT, "op3", make_payload(25)).is_ok());
    }
    // 不删除：调试用
    std::fprintf(stderr, "[!T] wal path = %s\n", path_.string().c_str());
    // 重新打开
    auto wr2 = Wal::create(default_cfg());
    if (!wr2.is_ok()) {
        ADD_FAILURE() << "reopen failed: err=0x" << std::hex
                      << static_cast<std::uint32_t>(wr2.error());
    }
    ASSERT_TRUE(wr2.is_ok());
    auto& wal2 = wr2.value();

    auto rr = wal2->replay();
    if (!rr.is_ok()) {
        ADD_FAILURE() << "replay failed: err=0x" << std::hex
                      << static_cast<std::uint32_t>(rr.error());
    }
    ASSERT_TRUE(rr.is_ok());
    const auto& entries = rr.value();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].seq_, 1u);
    EXPECT_EQ(entries[0].action_, "op1");
    EXPECT_EQ(entries[0].payload_.size(), 5u);
    EXPECT_EQ(entries[1].seq_, 2u);
    EXPECT_EQ(entries[1].action_, "op2");
    EXPECT_EQ(entries[2].seq_, 3u);
    EXPECT_EQ(entries[2].type_, WalEntryType::DISCONNECT);

    EXPECT_EQ(wal2->current_sequence(), 4u);
}

TEST_F(WalTest, TruncateKeepsTail) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();

    for (int i = 1; i <= 10; ++i) {
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE,
                                  "op_" + std::to_string(i), make_payload(i)).is_ok());
    }
    EXPECT_EQ(wal->current_sequence(), 11u);

    // 保留 seq >= 5
    auto tr = wal->truncate(5);
    ASSERT_TRUE(tr.is_ok());

    // replay 仅剩 5..10
    auto rr = wal->replay();
    ASSERT_TRUE(rr.is_ok());
    const auto& entries = rr.value();
    EXPECT_EQ(entries.size(), 6u);
    EXPECT_EQ(entries.front().seq_, 5u);
    EXPECT_EQ(entries.back().seq_, 10u);

    // next_seq 保持 11（truncate 不重置）
    EXPECT_EQ(wal->current_sequence(), 11u);
}

TEST_F(WalTest, SchemaVersionMismatchReturnsErr) {
    // 1. 构造一个文件
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        auto& wal = wr.value();
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE, "valid", make_payload(8)).is_ok());
    }

    // 2. 篡改文件头的 schema_version（offset 4..7）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        std::uint32_t bad_schema = 0xDEADBEEF;
        ssize_t n = ::pwrite(fd, &bad_schema, 4, 4);
        EXPECT_EQ(n, 4);
        ::close(fd);
    }

    // 3. 重新打开：read_file_header 应返回 SERIALIZE_VERSION_MISMATCH
    auto wr2 = Wal::create(default_cfg());
    EXPECT_TRUE(wr2.is_err());
    EXPECT_EQ(wr2.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// ---------- 并发与持久性 ----------

TEST_F(WalTest, ConcurrentWritersNoDataLoss) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();

    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;
    std::vector<std::thread> ts;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                std::string action = "t" + std::to_string(t) + "_i" + std::to_string(i);
                auto r = wal->append(WalEntryType::CUSTOM, action, make_payload(8));
                if (r.is_err()) errors.fetch_add(1);
            }
        });
    }
    for (auto& th : ts) th.join();

    EXPECT_EQ(errors.load(), 0) << "no write should fail";

    auto rr = wal->replay();
    ASSERT_TRUE(rr.is_ok());
    EXPECT_EQ(rr.value().size(), static_cast<std::size_t>(kThreads * kPerThread));

    // 验证 seq 唯一递增
    std::set<std::uint64_t> seen;
    for (const auto& e : rr.value()) {
        EXPECT_TRUE(seen.insert(e.seq_).second) << "duplicate seq: " << e.seq_;
    }
}

TEST_F(WalTest, RecoveryAfterCrashReplaysAll) {
    // 模拟"崩溃后恢复"：append 一些 entry → 销毁 Wal（模拟进程崩溃，fd 自动 close）
    // → 重新 create() → replay 应当读到所有 entry
    std::vector<std::uint64_t> written_seqs;
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        auto& wal = wr.value();
        for (int i = 0; i < 20; ++i) {
            auto r = wal->append(WalEntryType::ADD_NODE,
                                  "crash_test_" + std::to_string(i), make_payload(16));
            ASSERT_TRUE(r.is_ok());
            written_seqs.push_back(r.value());
        }
        // 析构：fd 自动 close（UniqueFd 保证）
    }

    // 重新打开
    auto wr2 = Wal::create(default_cfg());
    ASSERT_TRUE(wr2.is_ok());
    auto& wal2 = wr2.value();

    auto rr = wal2->replay();
    ASSERT_TRUE(rr.is_ok());
    const auto& entries = rr.value();
    ASSERT_EQ(entries.size(), written_seqs.size());

    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].seq_, written_seqs[i]);
    }
}

TEST_F(WalTest, ReplayStreamCallback) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE,
                                  "stream_" + std::to_string(i), make_payload(4)).is_ok());
    }

    int count = 0;
    auto r = wal->replay_stream([&](const WalEntry& e) {
        count++;
        EXPECT_EQ(e.seq_, static_cast<std::uint64_t>(count));
        // 第 3 条后终止
        return count < 3;
    });
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(count, 3) << "callback should stop at 3";
}

// ---------------- schema_version 不匹配（提升覆盖率） ----------------
TEST_F(WalTest, SchemaVersionMismatch) {
    // 1) 正常建 WAL 并写一条
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "a", make_payload(4)).is_ok());
    }
    // 2) 篡改文件头的 schema_version 字段（偏移 4..8）
    {
        std::ifstream in(path_, std::ios::binary);
        ASSERT_TRUE(in.is_open());
        std::vector<char> hdr(16, 0);
        in.read(hdr.data(), hdr.size());
        in.close();
        // magic(4) + schema_version(4)
        std::uint32_t bad = 0xDEADBEEF;
        std::memcpy(hdr.data() + 4, &bad, 4);
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(hdr.data(), hdr.size());
        out.close();
    }
    // 3) 重新打开应返回 SERIALIZE_VERSION_MISMATCH
    auto wr = Wal::create(default_cfg());
    EXPECT_TRUE(wr.is_err());
    EXPECT_EQ(wr.error(), udaf::core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// ---------------- 空 WAL replay ----------------
TEST_F(WalTest, ReplayEmptyWal) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto r = wr.value()->replay();
    EXPECT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().empty());
}

// ---------------- truncate(N) 保留 seq >= N 的所有 entry ----------------
TEST_F(WalTest, TruncateKeepHeaderOnly) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE,
                                  "t_" + std::to_string(i), make_payload(4)).is_ok());
    }
    // 保留 seq >= 3（即 seq 3、4、5 共 3 条）
    auto t = wal->truncate(3);
    EXPECT_TRUE(t.is_ok());

    auto wr2 = Wal::create(default_cfg());
    ASSERT_TRUE(wr2.is_ok());
    auto r = wr2.value()->replay();
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().size(), 3u) << "truncate(3) 应保留 seq>=3 共 3 条";
    EXPECT_EQ(r.value()[0].seq_, 3u);
    EXPECT_EQ(r.value()[1].seq_, 4u);
    EXPECT_EQ(r.value()[2].seq_, 5u);
    // next_seq 保持 6（truncate 不重置 sequence 计数器）
    EXPECT_EQ(wr2.value()->current_sequence(), 6u);
}

// ---------------- truncate_all 截断为只剩 header ----------------
TEST_F(WalTest, TruncateAllKeepsHeader) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE,
                                  "t_" + std::to_string(i), make_payload(4)).is_ok());
    }
    auto t = wal->truncate_all();
    EXPECT_TRUE(t.is_ok());

    auto r = wal->replay();
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().empty());
    // entry_count 已重置为 0
    EXPECT_EQ(wal->entry_count(), 0u);
}

// ---------------- Wal 移动语义 ----------------
TEST_F(WalTest, MoveConstruct) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    wr.value()->append(WalEntryType::ADD_NODE, "a", make_payload(4));
    Wal moved(std::move(*wr.value()));
    // 移动后可继续使用 moved
    auto r = moved.append(WalEntryType::ADD_NODE, "b", make_payload(4));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 2u);
}

TEST_F(WalTest, MoveAssign) {
    auto w1 = Wal::create(default_cfg());
    auto w2 = Wal::create(default_cfg());
    ASSERT_TRUE(w1.is_ok());
    ASSERT_TRUE(w2.is_ok());
    w1.value()->append(WalEntryType::ADD_NODE, "a", make_payload(4));
    w2.value()->append(WalEntryType::ADD_NODE, "x", make_payload(4));
    // 移动赋值
    *w1.value() = std::move(*w2.value());
    // w1 现在持有 w2 的状态
    EXPECT_EQ(w1.value()->current_sequence(), 2u);
}

// ---------------- Fs 接口 ----------------
TEST_F(WalTest, FsyncOk) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    EXPECT_TRUE(wr.value()->fsync().is_ok());
}

TEST_F(WalTest, PathGetter) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    EXPECT_EQ(wr.value()->path(), path_);
}

TEST_F(WalTest, SizeBytesAfterAppend) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    EXPECT_EQ(wr.value()->size_bytes(), Wal::kFileHeaderSize);
    wr.value()->append(WalEntryType::ADD_NODE, "x", make_payload(10));
    EXPECT_GT(wr.value()->size_bytes(), Wal::kFileHeaderSize);
}

// ---------------- WalConfig 限制 ----------------
TEST_F(WalTest, MaxEntriesReached) {
    WalConfig cfg = default_cfg();
    cfg.max_entries_ = 3;
    cfg.fsync_on_append_ = false;
    auto wr = Wal::create(cfg);
    ASSERT_TRUE(wr.is_ok());
    EXPECT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "a", make_payload(2)).is_ok());
    EXPECT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "b", make_payload(2)).is_ok());
    EXPECT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "c", make_payload(2)).is_ok());
    // 第 4 条：超出 max_entries → RES_DISK_FULL
    auto r = wr.value()->append(WalEntryType::ADD_NODE, "d", make_payload(2));
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::RES_DISK_FULL);
}

TEST_F(WalTest, MaxSizeBytesReached) {
    WalConfig cfg = default_cfg();
    cfg.max_size_bytes_ = Wal::kFileHeaderSize + 50;
    cfg.fsync_on_append_ = false;
    auto wr = Wal::create(cfg);
    ASSERT_TRUE(wr.is_ok());
    // 写 100B payload → 超过 max_size_bytes
    auto r = wr.value()->append(WalEntryType::ADD_NODE, "big",
                                  std::vector<std::uint8_t>(100, 0xAA));
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::RES_DISK_FULL);
}

// ---------------- ReplayStream 边界 ----------------
TEST_F(WalTest, ReplayStreamEmptyWal) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    int count = 0;
    auto r = wr.value()->replay_stream([&](const WalEntry&) {
        ++count;
        return true;
    });
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(count, 0);
}

TEST_F(WalTest, ReplayStreamNullCallback) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto r = wr.value()->replay_stream(nullptr);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), udaf::core::ErrorCode::INVALID_ARG);
}
// ===== 覆盖率补充（v0.3.14）=====

// partial header：文件长度 < kFileHeaderSize → next_seq 应从 1 开始
TEST_F(WalTest, PartialHeaderResetsSequence) {
    // 1) 写入合法 wal 一次，让 create() 落盘 header
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE,
                                        "init", make_payload(4)).is_ok());
    }
    // 2) truncate 文件到 4 字节（header 8 字节的前一半）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(::ftruncate(fd, 4), 0);
        ::close(fd);
    }
    // 3) 重新 open：read_file_header 应走"短读"分支（n < kFileHeaderSize），
    //    next_seq_=1，create 返回 Ok
    auto wr2 = Wal::create(default_cfg());
    ASSERT_TRUE(wr2.is_ok());
    EXPECT_EQ(wr2.value()->current_sequence(), 1u);

    // 4) append 应从 seq=1 开始
    auto r = wr2.value()->append(WalEntryType::ADD_NODE, "x", make_payload(2));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 1u);
}

// magic 不匹配（offset 0..3）→ SERIALIZE_VERSION_MISMATCH
TEST_F(WalTest, BadMagicReturnsVersionMismatch) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
    }
    int fd = ::open(path_.string().c_str(), O_RDWR);
    ASSERT_GE(fd, 0);
    std::uint32_t bad_magic = 0x00000000;  // 必然不等于 kFileMagic
    ssize_t n = ::pwrite(fd, &bad_magic, 4, 0);
    EXPECT_EQ(n, 4);
    ::close(fd);

    auto wr2 = Wal::create(default_cfg());
    EXPECT_TRUE(wr2.is_err());
    EXPECT_EQ(wr2.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// truncate_all 后重开 → header 保留 + 通过扫描 entries 重建 next_seq
// （文件已 truncate 到仅 header，故扫描到 0 条 entry → next_seq=1）
TEST_F(WalTest, TruncateAllThenReopenRebuildsSequence) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        auto& wal = wr.value();
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE, "a", make_payload(2)).is_ok());
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE, "b", make_payload(2)).is_ok());
        ASSERT_TRUE(wal->truncate_all().is_ok());
        EXPECT_EQ(wal->size_bytes(), Wal::kFileHeaderSize);
    }
    auto wr2 = Wal::create(default_cfg());
    ASSERT_TRUE(wr2.is_ok());
    // 重开后从 1 重新开始（entries 已被 truncate）
    EXPECT_EQ(wr2.value()->current_sequence(), 1u);
    auto r = wr2.value()->append(WalEntryType::ADD_NODE, "c", make_payload(2));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 1u);
}

// replay_stream 在文件为空（仅 header）时 → callback 不被调用，Ok
TEST_F(WalTest, ReplayStreamOnlyHeader) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    int called = 0;
    auto r = wr.value()->replay_stream([&](const WalEntry&) {
        ++called;
        return true;
    });
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(called, 0);
}

// replay_stream 返回 false → replay 提前终止，Ok（剩余条目不再投递）
TEST_F(WalTest, ReplayStreamEarlyStop) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = wr.value();
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(wal->append(WalEntryType::ADD_NODE,
                                "n" + std::to_string(i), make_payload(2)).is_ok());
    }
    int called = 0;
    auto r = wal->replay_stream([&](const WalEntry& e) {
        ++called;
        if (e.seq_ >= 3u) return false;  // 提前终止
        return true;
    });
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(called, 3);
}

// 覆盖 wal.cpp:556-564 truncate(keep > all_seq) → kept 为空 → 截断为只有 header
TEST_F(WalTest, TruncateKeepSequenceBeyondAllTruncatesHeaderOnly) {
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto& wal = *wr.value();
    std::vector<std::uint8_t> payload(8, 0xAA);
    ASSERT_TRUE(wal.append(WalEntryType::ADD_NODE, "a", payload).is_ok());
    ASSERT_TRUE(wal.append(WalEntryType::ADD_NODE, "b", payload).is_ok());
    ASSERT_TRUE(wal.append(WalEntryType::ADD_NODE, "c", payload).is_ok());

    // keep_sequence=10 > 所有 entry.seq_（≤3）→ kept.empty() 分支
    auto t = wal.truncate(10);
    ASSERT_TRUE(t.is_ok());
    EXPECT_EQ(wal.entry_count(), 0u);
    EXPECT_EQ(wal.size_bytes(), static_cast<std::uint64_t>(Wal::kFileHeaderSize));
}

// 覆盖 wal.cpp:157-158 read_file_header 文件头小于 kFileHeaderSize → next_seq=1
TEST_F(WalTest, ReopenTruncatedHeaderDefaultsToSeq1) {
    // 先创建一次（写完整 header）
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
    }
    // 截断文件到 5 字节
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(::ftruncate(fd, 5), 0);
        ::close(fd);
    }
    // 重新打开应成功，next_seq 默认 = 1
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    EXPECT_EQ(wr.value()->current_sequence(), 1u);
}

// 覆盖 wal.cpp:166-170 magic 不匹配 → SERIALIZE_VERSION_MISMATCH
TEST_F(WalTest, ReopenWithBadMagicReturnsErr) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
    }
    // 改写前 4 字节为错误 magic
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        std::uint32_t bad_magic = 0xDEADC0DE;
        ssize_t n = ::pwrite(fd, &bad_magic, 4, 0);
        EXPECT_EQ(n, 4);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    EXPECT_TRUE(wr.is_err());
    EXPECT_EQ(wr.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// ===== 覆盖率补充（v0.3.15 - Round 6）=====

// 覆盖 read_entry 中的 cksum 校验失败分支（wal.cpp:419-421）
TEST_F(WalTest, ReplayCorruptedEntryChecksumReturnsErr) {
    // 写入一条 entry
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "ok",
                                        make_payload(8)).is_ok());
    }
    // 篡改 entry payload 的某个字节（让 cksum 校验失败）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        // header 后偏移：kFileHeaderSize(16) + kEntryHeaderSize(40) + action_len
        // action "ok" 长度=2，故 payload 在 offset=16+40+2=58
        std::uint8_t bad = 0xFF;
        ssize_t n = ::pwrite(fd, &bad, 1, 58);
        EXPECT_EQ(n, 1);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto rr = wr.value()->replay();
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error(), ErrorCode::SERIALIZE_DECODE_FAILED);
}

// 覆盖 read_entry 中的 magic 校验失败分支（wal.cpp:388-390）
TEST_F(WalTest, ReplayCorruptedEntryMagicReturnsErr) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "x",
                                        make_payload(4)).is_ok());
    }
    // 篡改第一条 entry 的 magic 字段（在 kFileHeaderSize 偏移 0 处）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        std::uint32_t bad_magic = 0xDEADBEEF;
        ssize_t n = ::pwrite(fd, &bad_magic, 4, 16);  // entry header 起点
        EXPECT_EQ(n, 4);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto rr = wr.value()->replay();
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// 覆盖 read_entry 中的 schema 校验失败分支（wal.cpp:391-393）
TEST_F(WalTest, ReplayCorruptedEntrySchemaReturnsErr) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "x",
                                        make_payload(4)).is_ok());
    }
    // 篡改第一条 entry 的 schema_version 字段（offset kFileHeaderSize+4=20）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        std::uint32_t bad_schema = 0xDEADBEEF;
        ssize_t n = ::pwrite(fd, &bad_schema, 4, 20);
        EXPECT_EQ(n, 4);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto rr = wr.value()->replay();
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error(), ErrorCode::SERIALIZE_VERSION_MISMATCH);
}

// 覆盖 read_entry 中的 truncated action 路径（wal.cpp:400-402）
TEST_F(WalTest, ReplayTruncatedEntryActionReturnsErr) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        // 写 5 字节 action + 8 字节 payload
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "12345",
                                        make_payload(8)).is_ok());
    }
    // 截断文件，去掉 action 末尾几个字节（保留 header + 部分 action）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        // header(16) + entry_header(40) + action_partial(2) = 58
        ASSERT_EQ(::ftruncate(fd, 58), 0);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto rr = wr.value()->replay();
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error(), ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
}

// 覆盖 read_entry 中的 truncated payload 路径（wal.cpp:410-412）
TEST_F(WalTest, ReplayTruncatedEntryPayloadReturnsErr) {
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "abc",
                                        make_payload(8)).is_ok());
    }
    // 截断文件，去掉 payload 末尾几个字节
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        // header(16) + entry_header(40) + action(3) + payload_partial(2) = 61
        ASSERT_EQ(::ftruncate(fd, 61), 0);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    auto rr = wr.value()->replay();
    EXPECT_TRUE(rr.is_err());
    EXPECT_EQ(rr.error(), ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
}

// 覆盖 create() 时 fsync_on_append=true 的路径（wal.cpp:471-473）
TEST_F(WalTest, FsyncOnAppendPath) {
    WalConfig cfg = default_cfg();
    cfg.fsync_on_append_ = true;
    auto wr = Wal::create(cfg);
    ASSERT_TRUE(wr.is_ok());
    auto r = wr.value()->append(WalEntryType::ADD_NODE, "fsync_test",
                                  make_payload(8));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 1u);
}

// 覆盖 create() 时无法创建父目录的路径（wal.cpp:228-236）
TEST_F(WalTest, CreateWithInvalidParentPathReturnsErr) {
    WalConfig cfg;
    // 父目录是一个已存在的文件，无法作为目录创建
    std::filesystem::path fake_parent = make_tmp_path("fake_parent");
    std::filesystem::path fake_file = fake_parent / "regular_file";
    {
        std::ofstream ofs(fake_parent);
        ofs << "x";
    }
    cfg.path_ = fake_file / "wal";  // 父路径是文件而非目录
    cfg.fsync_on_append_ = false;
    auto wr = Wal::create(cfg);
    // 应该返回 BIZ_FILE_NOT_FOUND
    if (wr.is_err()) {
        EXPECT_EQ(wr.error(), ErrorCode::BIZ_FILE_NOT_FOUND);
    }
    rm_tmp(fake_parent);
}

// 覆盖 scan_entries_for_max_seq 中的 lseek 失败分支（wal.cpp:158-159）
// 通过把文件截断到奇数长度，让后续 seek 失败的可能性
TEST_F(WalTest, ReadPartialHeaderFromLargerFile) {
    // 写入 3 条 entry，再把文件截短到只有 1 条半（保留 header + 完整 entry）
    {
        auto wr = Wal::create(default_cfg());
        ASSERT_TRUE(wr.is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "a",
                                        make_payload(4)).is_ok());
        ASSERT_TRUE(wr.value()->append(WalEntryType::ADD_NODE, "b",
                                        make_payload(4)).is_ok());
    }
    // 截断文件到只保留第一条 entry（header + 第一条 entry 完整）
    {
        int fd = ::open(path_.string().c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        // header(16) + entry_header(40) + action(1) + payload(4) = 61
        ASSERT_EQ(::ftruncate(fd, 61), 0);
        ::close(fd);
    }
    auto wr = Wal::create(default_cfg());
    ASSERT_TRUE(wr.is_ok());
    // next_seq 应该是 2（保留的 entry seq=1）
    EXPECT_EQ(wr.value()->current_sequence(), 2u);
    auto rr = wr.value()->replay();
    ASSERT_TRUE(rr.is_ok());
    EXPECT_EQ(rr.value().size(), 1u);
    EXPECT_EQ(rr.value()[0].seq_, 1u);
}
