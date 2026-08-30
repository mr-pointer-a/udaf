// wal.cpp - Wal 二进制 IO 完整实现
#include "wal.hpp"

#include "log/logger.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

namespace udaf::platform::fs {

namespace {

/// 当前纳秒时间戳（自 epoch）。
inline std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// POSIX open() 包装：处理 EINTR；目录不存在时返回 -1。
int open_wal_file(const std::filesystem::path& path, bool create) noexcept {
    const std::string p = path.string();
    int flags = O_APPEND | O_RDWR;
    if (create) flags |= O_CREAT;

    int fd = -1;
    do {
        fd = ::open(p.c_str(), flags, 0640);
    } while (fd < 0 && errno == EINTR);

    return fd;
}

/// pwrite() 包装：处理 EINTR；返回写入字节数，错误返回 -1。
ssize_t write_at(int fd, const void* buf, size_t len, off_t offset) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    std::size_t total = 0;
    while (total < len) {
        ssize_t n = ::pwrite(fd, p + total, len - total, offset + static_cast<off_t>(total));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total += static_cast<std::size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

/// pread() 包装：处理 EINTR；EOF 返回 0，错误返回 -1。
ssize_t read_at(int fd, void* buf, size_t len, off_t offset) noexcept {
    auto* p = static_cast<std::uint8_t*>(buf);
    std::size_t total = 0;
    while (total < len) {
        ssize_t n = ::pread(fd, p + total, len - total, offset + static_cast<off_t>(total));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;  // EOF
        total += static_cast<std::size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

/// pwrite 不支持 O_APPEND，写入前需要 seek 到末尾。
off_t seek_end(int fd) noexcept {
    return ::lseek(fd, 0, SEEK_END);
}

off_t seek_set(int fd, off_t off) noexcept {
    return ::lseek(fd, off, SEEK_SET);
}

/// FNV-1a 32-bit 哈希。
inline std::uint32_t fnv1a(const void* buf, std::size_t len) noexcept {
    std::uint32_t h = 0x811C9DC5U;
    const auto* p = static_cast<const std::uint8_t*>(buf);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x01000193U;
    }
    return h;
}

/// 截断文件到指定大小（ftruncate 包装，处理 EINTR）。
int ftruncate_wrapped(int fd, off_t length) noexcept {
    int ret = 0;
    do {
        ret = ::ftruncate(fd, length);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

}  // namespace

// ---------- 构造 / 析构 / 移动 ----------

Wal::Wal(WalConfig config, UniqueFd fd) noexcept
    : config_(std::move(config)), fd_(std::move(fd)) {}

Wal::~Wal() noexcept = default;

Wal::Wal(Wal&& other) noexcept
    : config_(std::move(other.config_)),
      fd_(std::move(other.fd_)),
      next_seq_(other.next_seq_.load()),
      size_bytes_(other.size_bytes_.load()),
      entry_count_(other.entry_count_.load()) {}

Wal& Wal::operator=(Wal&& other) noexcept {
    if (this != &other) {
        std::lock_guard<std::mutex> lk_self(mutex_);
        std::lock_guard<std::mutex> lk_other(other.mutex_);
        config_ = std::move(other.config_);
        fd_ = std::move(other.fd_);
        next_seq_.store(other.next_seq_.load(), std::memory_order_relaxed);
        size_bytes_.store(other.size_bytes_.load(), std::memory_order_relaxed);
        entry_count_.store(other.entry_count_.load(), std::memory_order_relaxed);
    }
    return *this;
}

// ---------- 文件头 ----------

core::Result<void> Wal::write_file_header() noexcept {
    std::uint8_t buf[kFileHeaderSize];
    std::uint32_t magic = kFileMagic;
    std::uint32_t schema = WalEntry::kSchemaVersion;
    std::uint64_t next_seq = 0;

    std::memcpy(buf + 0, &magic, 4);
    std::memcpy(buf + 4, &schema, 4);
    std::memcpy(buf + 8, &next_seq, 8);

    ssize_t n = write_at(fd_.get(), buf, kFileHeaderSize, 0);
    if (n < 0 || static_cast<std::size_t>(n) != kFileHeaderSize) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    size_bytes_.store(kFileHeaderSize, std::memory_order_relaxed);
    return core::Result<void>::ok();
}

core::Result<std::uint64_t> Wal::read_file_header() noexcept {
    std::uint8_t buf[kFileHeaderSize];
    ssize_t n = read_at(fd_.get(), buf, kFileHeaderSize, 0);
    if (n < 0) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }
    if (static_cast<std::size_t>(n) < kFileHeaderSize) {
        next_seq_.store(1, std::memory_order_relaxed);
        return core::Result<std::uint64_t>::ok(1);
    }

    std::uint32_t magic = 0;
    std::uint32_t schema = 0;
    std::memcpy(&magic, buf + 0, 4);
    std::memcpy(&schema, buf + 4, 4);

    if (magic != kFileMagic) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error, "Wal file magic mismatch",
            core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
        return core::Result<std::uint64_t>::err(core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }
    if (schema != WalEntry::kSchemaVersion) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error, "Wal file schema_version mismatch",
            core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
        return core::Result<std::uint64_t>::err(core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }

    struct stat st{};
    if (::fstat(fd_.get(), &st) == 0) {
        size_bytes_.store(static_cast<std::uint64_t>(st.st_size), std::memory_order_relaxed);
    }
    // next_seq 通过扫描 entries 计算（避免 O_APPEND 模式下 pwrite 破坏布局的问题）
    std::uint64_t max_seq = 0;
    std::uint64_t count = 0;
    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) >= 0) {
        while (true) {
            off_t cur = ::lseek(fd_.get(), 0, SEEK_CUR);
            struct stat st2{};
            if (::fstat(fd_.get(), &st2) < 0) break;
            if (cur >= st2.st_size) break;

            std::uint8_t hdr[kEntryHeaderSize];
            ssize_t hn = read_at(fd_.get(), hdr, kEntryHeaderSize, cur);
            if (hn < static_cast<ssize_t>(kEntryHeaderSize)) break;
            std::uint32_t hm = 0, hs = 0;
            std::uint64_t hsq = 0;
            std::uint32_t al = 0, pl = 0;
            std::memcpy(&hm, hdr + 0, 4);
            std::memcpy(&hs, hdr + 4, 4);
            std::memcpy(&hsq, hdr + 8, 8);
            std::memcpy(&al, hdr + 25, 4);
            std::memcpy(&pl, hdr + 29, 4);
            if (hm != kFileMagic || hs != WalEntry::kSchemaVersion) break;
            if (hsq > max_seq) max_seq = hsq;
            ++count;
            off_t next = cur + static_cast<off_t>(kEntryHeaderSize + al + pl);
            if (::lseek(fd_.get(), next, SEEK_SET) < 0) break;
        }
    }
    entry_count_.store(count, std::memory_order_relaxed);
    const std::uint64_t cur = max_seq + 1;
    next_seq_.store(cur == 0 ? 1 : cur, std::memory_order_relaxed);
    ::lseek(fd_.get(), static_cast<off_t>(kFileHeaderSize), SEEK_SET);
    return core::Result<std::uint64_t>::ok(cur == 0 ? 1 : cur);
}

// ---------- 工厂方法 ----------

core::Result<std::unique_ptr<Wal>> Wal::create(WalConfig config) noexcept {
    namespace fs = std::filesystem;

    if (config.path_.has_parent_path()) {
        const fs::path parent = config.path_.parent_path();
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec && !fs::exists(parent)) {
            return core::Result<std::unique_ptr<Wal>>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
        }
        ::chmod(parent.c_str(), 0750);  // best-effort
    }

    const bool exists = fs::exists(config.path_);
    UniqueFd owned_fd(open_wal_file(config.path_, !exists));
    if (!owned_fd) {
        udaf::core::Logger::instance().log_with_error(
            core::LogLevel::Error, "Wal open failed", core::ErrorCode::BIZ_FILE_NOT_FOUND);
        return core::Result<std::unique_ptr<Wal>>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
    }

    auto wal = std::unique_ptr<Wal>(new Wal(std::move(config), std::move(owned_fd)));

    if (!exists) {
        auto wr = wal->write_file_header();
        if (wr.is_err()) {
            return core::Result<std::unique_ptr<Wal>>::err(wr.error());
        }
        wal->next_seq_.store(1, std::memory_order_relaxed);
    } else {
        auto rr = wal->read_file_header();
        if (rr.is_err()) {
            return core::Result<std::unique_ptr<Wal>>::err(rr.error());
        }
    }
    return core::Result<std::unique_ptr<Wal>>::ok(std::move(wal));
}

// ---------- Entry IO ----------

core::Result<std::uint64_t> Wal::write_entry(const WalEntry& entry) noexcept {
    // 构造 header（40 字节固定）
    std::uint8_t header[kEntryHeaderSize];
    std::uint32_t magic   = kFileMagic;
    std::uint32_t schema  = entry.schema_version_;
    std::uint64_t seq     = entry.seq_;
    std::int64_t  ts      = entry.timestamp_ns_;
    std::uint8_t  type    = static_cast<std::uint8_t>(entry.type_);
    std::uint32_t act_len = static_cast<std::uint32_t>(entry.action_.size());
    std::uint32_t pay_len = static_cast<std::uint32_t>(entry.payload_.size());

    std::memcpy(header + 0,  &magic,   4);
    std::memcpy(header + 4,  &schema,  4);
    std::memcpy(header + 8,  &seq,     8);
    std::memcpy(header + 16, &ts,      8);
    std::memcpy(header + 24, &type,    1);
    std::memcpy(header + 25, &act_len, 4);
    std::memcpy(header + 29, &pay_len, 4);
    // 33-35 保留（清零）
    header[33] = 0; header[34] = 0; header[35] = 0;

    // 校验位 = FNV-1a(header[0..35]) ^ FNV-1a(action) ^ FNV-1a(payload)
    std::uint32_t cksum = fnv1a(header, 36);
    if (act_len > 0) cksum ^= fnv1a(entry.action_.data(), act_len);
    if (pay_len > 0) cksum ^= fnv1a(entry.payload_.data(), pay_len);
    std::memcpy(header + 36, &cksum, 4);

    // seek 到末尾（O_APPEND + pwrite 在某些平台不兼容，统一显式 seek）
    off_t end_off = seek_end(fd_.get());
    if (end_off < 0) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }

    ssize_t n1 = write_at(fd_.get(), header, kEntryHeaderSize, end_off);
    if (n1 < 0 || static_cast<std::size_t>(n1) != kEntryHeaderSize) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }
    // 调试：回读 40 字节验证
    {
        std::uint8_t verify[40];
        ssize_t nv = read_at(fd_.get(), verify, 40, end_off);
        if (nv == 40 && std::memcmp(verify, header, 40) != 0) {
            char dbg[512];
            int o = std::snprintf(dbg, sizeof(dbg), "[!W] full hdr mismatch (40B):\n  file:  ");
            for (int i = 0; i < 40; ++i) o += std::snprintf(dbg + o, sizeof(dbg) - o, "%02X ", verify[i]);
            o += std::snprintf(dbg + o, sizeof(dbg) - o, "\n  expect:");
            for (int i = 0; i < 40; ++i) o += std::snprintf(dbg + o, sizeof(dbg) - o, "%02X ", header[i]);
            std::snprintf(dbg + o, sizeof(dbg) - o, "\n");
            std::fputs(dbg, stderr);
            std::fflush(stderr);
        }
    }
    if (act_len > 0) {
        ssize_t n2 = write_at(fd_.get(), entry.action_.data(), act_len,
                              end_off + static_cast<off_t>(kEntryHeaderSize));
        if (n2 < 0 || static_cast<std::size_t>(n2) != act_len) {
            return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
        }
    }
    if (pay_len > 0) {
        ssize_t n3 = write_at(fd_.get(), entry.payload_.data(), pay_len,
                              end_off + static_cast<off_t>(kEntryHeaderSize) + static_cast<off_t>(act_len));
        if (n3 < 0 || static_cast<std::size_t>(n3) != pay_len) {
            return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
        }
    }

    // 更新统计
    const std::uint64_t written = kEntryHeaderSize + act_len + pay_len;
    size_bytes_.fetch_add(written, std::memory_order_relaxed);
    entry_count_.fetch_add(1, std::memory_order_relaxed);
    return core::Result<std::uint64_t>::ok(entry.seq_);
}

core::Result<WalEntry> Wal::read_entry() noexcept {
    WalEntry e;
    std::uint8_t header[kEntryHeaderSize];

    // 获取当前 fd 偏移
    off_t cur = ::lseek(fd_.get(), 0, SEEK_CUR);
    if (cur < 0 || static_cast<std::size_t>(cur) < kFileHeaderSize) {
        return core::Result<WalEntry>::err(core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
    }
    if (seek_set(fd_.get(), cur) < 0) {
        return core::Result<WalEntry>::err(core::ErrorCode::INTERNAL);
    }

    ssize_t n = read_at(fd_.get(), header, kEntryHeaderSize, cur);
    if (n < 0) {
        return core::Result<WalEntry>::err(core::ErrorCode::INTERNAL);
    }
    if (static_cast<std::size_t>(n) < kEntryHeaderSize) {
        return core::Result<WalEntry>::err(core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
    }

    std::uint32_t magic = 0;
    std::uint32_t schema = 0;
    std::uint64_t seq = 0;
    std::int64_t  ts = 0;
    std::uint8_t  type = 0;
    std::uint32_t act_len = 0;
    std::uint32_t pay_len = 0;
    std::uint32_t cksum_read = 0;

    std::memcpy(&magic,   header + 0,  4);
    std::memcpy(&schema,  header + 4,  4);
    std::memcpy(&seq,     header + 8,  8);
    std::memcpy(&ts,      header + 16, 8);
    std::memcpy(&type,    header + 24, 1);
    std::memcpy(&act_len, header + 25, 4);
    std::memcpy(&pay_len, header + 29, 4);
    std::memcpy(&cksum_read, header + 36, 4);

    if (magic != kFileMagic) {
        return core::Result<WalEntry>::err(core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }
    if (schema != WalEntry::kSchemaVersion) {
        return core::Result<WalEntry>::err(core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }

    std::string action;
    if (act_len > 0) {
        action.resize(act_len);
        ssize_t na = read_at(fd_.get(), action.data(), act_len,
                             cur + static_cast<off_t>(kEntryHeaderSize));
        if (na < 0 || static_cast<std::size_t>(na) != act_len) {
            return core::Result<WalEntry>::err(core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
        }
    }
    std::vector<std::uint8_t> payload;
    if (pay_len > 0) {
        payload.resize(pay_len);
        ssize_t np = read_at(fd_.get(), payload.data(), pay_len,
                             cur + static_cast<off_t>(kEntryHeaderSize) +
                             static_cast<off_t>(act_len));
        if (np < 0 || static_cast<std::size_t>(np) != pay_len) {
            return core::Result<WalEntry>::err(core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
        }
    }

    // 校验位
    std::uint32_t cksum_calc = fnv1a(header, 36);
    if (act_len > 0) cksum_calc ^= fnv1a(action.data(), act_len);
    if (pay_len > 0) cksum_calc ^= fnv1a(payload.data(), pay_len);
    if (cksum_calc != cksum_read) {
        return core::Result<WalEntry>::err(core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }

    // seek 到下一条 entry 起点
    off_t next = cur + static_cast<off_t>(kEntryHeaderSize + act_len + pay_len);
    ::lseek(fd_.get(), next, SEEK_SET);

    e.schema_version_ = schema;
    e.seq_ = seq;
    e.timestamp_ns_ = ts;
    e.type_ = static_cast<WalEntryType>(type);
    e.action_ = std::move(action);
    e.payload_ = std::move(payload);
    return core::Result<WalEntry>::ok(std::move(e));
}

// ---------- 公开接口 ----------

core::Result<std::uint64_t> Wal::append(WalEntryType type,
                                        const std::string& action,
                                        std::span<const std::uint8_t> payload) noexcept {
    std::lock_guard<std::mutex> lk(mutex_);

    WalEntry e;
    e.schema_version_ = WalEntry::kSchemaVersion;
    e.seq_            = next_seq_.fetch_add(1, std::memory_order_relaxed);
    e.timestamp_ns_   = now_ns();
    e.type_           = type;
    e.action_         = action;
    e.payload_.assign(payload.begin(), payload.end());

    if (config_.max_entries_ > 0 && entry_count_.load() >= config_.max_entries_) {
        next_seq_.fetch_sub(1, std::memory_order_relaxed);
        return core::Result<std::uint64_t>::err(core::ErrorCode::RES_DISK_FULL);
    }
    if (config_.max_size_bytes_ > 0 &&
        size_bytes_.load() + kEntryHeaderSize + action.size() + payload.size() > config_.max_size_bytes_) {
        next_seq_.fetch_sub(1, std::memory_order_relaxed);
        return core::Result<std::uint64_t>::err(core::ErrorCode::RES_DISK_FULL);
    }

    auto wr = write_entry(e);
    if (wr.is_err()) {
        next_seq_.fetch_sub(1, std::memory_order_relaxed);
        return wr;
    }

    // 注：不在此处回写 file header 的 next_seq 字段。O_APPEND 模式下 pwrite
    // 可能被强制写入文件末尾（破坏布局），而显式 lseek+write 同样受 O_APPEND
    // 影响。next_seq 在 create() 时通过扫描 entries 计算（见下）。

    if (config_.fsync_on_append_) {
        ::fsync(fd_.get());
    }
    return core::Result<std::uint64_t>::ok(e.seq_);
}

core::Result<void> Wal::fsync() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    int ret = ::fsync(fd_.get());
    if (ret < 0 && errno != EINTR) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<void>::ok();
}

core::Result<std::vector<WalEntry>> Wal::replay() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);

    // seek 到第一条 entry 起点
    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<std::vector<WalEntry>>::err(core::ErrorCode::INTERNAL);
    }

    std::vector<WalEntry> out;
    while (true) {
        off_t cur = ::lseek(fd_.get(), 0, SEEK_CUR);
        struct stat st{};
        if (::fstat(fd_.get(), &st) < 0) {
            return core::Result<std::vector<WalEntry>>::err(core::ErrorCode::INTERNAL);
        }
        if (cur >= st.st_size) break;  // EOF

        auto r = read_entry();
        if (r.is_err()) {
            return core::Result<std::vector<WalEntry>>::err(r.error());
        }
        out.push_back(std::move(r).value());
    }

    // 更新 entry_count
    entry_count_.store(out.size(), std::memory_order_relaxed);
    return core::Result<std::vector<WalEntry>>::ok(std::move(out));
}

core::Result<void> Wal::replay_stream(WalCallback callback) noexcept {
    if (!callback) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    std::lock_guard<std::mutex> lk(mutex_);

    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    while (true) {
        off_t cur = ::lseek(fd_.get(), 0, SEEK_CUR);
        struct stat st{};
        if (::fstat(fd_.get(), &st) < 0) {
            return core::Result<void>::err(core::ErrorCode::INTERNAL);
        }
        if (cur >= st.st_size) break;

        auto r = read_entry();
        if (r.is_err()) {
            return core::Result<void>::err(r.error());
        }
        if (!callback(r.value())) {
            break;
        }
    }
    return core::Result<void>::ok();
}

core::Result<void> Wal::truncate(std::uint64_t keep_sequence) noexcept {
    std::lock_guard<std::mutex> lk(mutex_);

    // seek 到第一条 entry
    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    std::vector<WalEntry> kept;
    std::uint64_t kept_bytes = kFileHeaderSize;
    while (true) {
        off_t cur = ::lseek(fd_.get(), 0, SEEK_CUR);
        struct stat st{};
        if (::fstat(fd_.get(), &st) < 0) {
            return core::Result<void>::err(core::ErrorCode::INTERNAL);
        }
        if (cur >= st.st_size) break;

        auto r = read_entry();
        if (r.is_err()) {
            return core::Result<void>::err(r.error());
        }
        const auto& e = r.value();
        if (e.seq_ >= keep_sequence) {
            kept_bytes += kEntryHeaderSize + e.action_.size() + e.payload_.size();
            kept.push_back(std::move(e));
        }
    }

    if (kept.empty()) {
        // 没有需要保留的：截断为只有 header
        if (ftruncate_wrapped(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
            return core::Result<void>::err(core::ErrorCode::INTERNAL);
        }
        entry_count_.store(0, std::memory_order_relaxed);
        size_bytes_.store(kFileHeaderSize, std::memory_order_relaxed);
        // next_seq 不变（仍为 max+1）
        return core::Result<void>::ok();
    }

    // 重写文件：header + 保留的 entries
    // 简化实现：截断到 kept_bytes，然后重新追加
    if (ftruncate_wrapped(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    for (const auto& e : kept) {
        auto wr = write_entry(e);
        if (wr.is_err()) {
            return core::Result<void>::err(wr.error());
        }
    }

    entry_count_.store(kept.size(), std::memory_order_relaxed);
    size_bytes_.store(kept_bytes, std::memory_order_relaxed);
    return core::Result<void>::ok();
}

core::Result<void> Wal::truncate_all() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    if (ftruncate_wrapped(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    if (seek_set(fd_.get(), static_cast<off_t>(kFileHeaderSize)) < 0) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    entry_count_.store(0, std::memory_order_relaxed);
    size_bytes_.store(kFileHeaderSize, std::memory_order_relaxed);
    // next_seq 保持（下一个 append 仍递增）
    return core::Result<void>::ok();
}

std::uint64_t Wal::current_sequence() const noexcept {
    return next_seq_.load(std::memory_order_relaxed);
}

std::uint64_t Wal::size_bytes() const noexcept {
    return size_bytes_.load(std::memory_order_relaxed);
}

}  // namespace udaf::platform::fs