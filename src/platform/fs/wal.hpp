// wal.hpp - UDAF Write-Ahead Log（拓扑/审计之外的通用 WAL）
//
// 设计要点（融合 03 §6.3.2 + 04 §2.8.2）：
//   - 单文件追加写，二进制格式（每条 Entry: header + payload）
//   - schema_version 校验：replay 时不匹配 → SERIALIZE_VERSION_MISMATCH
//   - append(type, payload) → Result<uint64_t>（新 sequence）
//   - replay() → Result<vector<Entry>>（按 seq 升序）
//   - truncate(keep_sequence) → 保留 >= keep_sequence 的记录
//   - fsync() → Result<void>：强制落盘
//   - Rule of Five：禁用拷贝 + noexcept 移动
//   - 线程安全：std::mutex 串行化所有 IO
//   - 不抛异常（CLAUDE.md §3.5）
//
// 应用场景：
//   - Topology 事务日志（详见 03 §3.2）
//   - AuditLogger 之外的业务 WAL（审计日志独立成 udaf::audit）

#ifndef UDAF_PLATFORM_FS_WAL_HPP
#define UDAF_PLATFORM_FS_WAL_HPP

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "core/error_code.hpp"
#include "platform/fs/unique_fd.hpp"

namespace udaf::platform::fs {

/// WAL 条目类型（与 03 §6.3.2 + 03 §10 链路一致）
enum class WalEntryType : std::uint8_t {
    ADD_NODE       = 1,
    REMOVE_NODE    = 2,
    CONNECT        = 3,
    DISCONNECT     = 4,
    LIFECYCLE_STATE = 5,
    CUSTOM          = 100,  // 业务自定义类型起点
};

/// Wal::Entry 单条记录（字段尾下划线对齐 CLAUDE.md §2）。
struct WalEntry {  // NOLINT(clang-analyzer-core.uninitialized.Assign)
    // kSchemaVersion 是 static constexpr inline 变量，clang-analyzer 误报"未初始化"
    /// 与 03 §6.3.2 SCHEMA_VERSION 对齐（保持 ABI 兼容）。
    static constexpr std::uint32_t kSchemaVersion = 1;

    std::uint32_t schema_version_ = kSchemaVersion;  ///< 4 字节；replay 校验
    std::uint64_t seq_             = 0;                ///< 全局唯一递增 sequence
    std::int64_t  timestamp_ns_    = 0;                ///< 自 epoch 起纳秒
    WalEntryType  type_            = WalEntryType::CUSTOM;
    std::string   action_;                            ///< 可读字符串（拓扑：add_node/remove_edge ...）
    std::vector<std::uint8_t> payload_;               ///< 序列化后的参数
};

/// Wal 公开配置
struct WalConfig {
    std::filesystem::path path_;            ///< WAL 文件路径
    std::uint64_t max_size_bytes_ = 0;      ///< 单文件最大字节；0 = 不限制（仅靠 truncate 控制）
    std::uint32_t max_entries_   = 0;      ///< 单文件最大条目；0 = 不限制
    bool fsync_on_append_         = true;   ///< append 后立即 fsync（性能 vs 安全性权衡）
};

/// WalCallback：replay 流式回调签名（每条记录触发一次）。
/// 返回 false 可终止 replay。
using WalCallback = std::function<bool(const WalEntry&)>;

/// Write-Ahead Log 类。
class Wal {
public:
    /// 文件 magic（用于校验文件类型 + 损坏检测）
    static constexpr std::uint32_t kFileMagic = 0xDA1F00D5U;

    /// 文件头大小（字节）
    static constexpr std::size_t kFileHeaderSize = 16;

    /// 单条 Entry 头部大小（不含 payload）
    ///   magic(4) + schema_version(4) + seq(8) + timestamp_ns(8) = 24
    ///   type(1) + action_len(4) + payload_len(4) + reserved(3) + checksum(4) = 16
    ///   合计：40 字节
    static constexpr std::size_t kEntryHeaderSize = 40;

    // ---------- 工厂方法 ----------

    /// 创建并打开 WAL：若文件不存在则创建；存在则追加。
    /// @return Ok(unique_ptr<Wal>) / Err(IO 错误)
    [[nodiscard]] static core::Result<std::unique_ptr<Wal>> create(WalConfig config) noexcept;

    // ---------- Rule of Five ----------

    Wal(const Wal&)            = delete;
    Wal& operator=(const Wal&) = delete;

    Wal(Wal&& other) noexcept;
    Wal& operator=(Wal&& other) noexcept;
    ~Wal() noexcept;

    // ---------- 写接口 ----------

    /// 追加一条 Entry。
    /// @param type 条目类型
    /// @param action 可读字符串（如 "add_node"）
    /// @param payload 二进制参数（拓扑为 serialize(NodeSpec) 等）
    /// @return Ok(新 sequence)；Err(IO / 容量超限)
    [[nodiscard]] core::Result<std::uint64_t> append(WalEntryType type,
                                                     const std::string& action,
                                                     std::span<const std::uint8_t> payload) noexcept;

    /// 强制刷盘（fsync）。
    [[nodiscard]] core::Result<void> fsync() noexcept;

    // ---------- 读接口 ----------

    /// 读取所有 Entry 到 vector。
    /// @return Ok(vector<Entry>)；Err(SERIALIZE_VERSION_MISMATCH / IO 错误)
    /// @note 不匹配 schema_version 时立即返回 SERIALIZE_VERSION_MISMATCH（03 §6.3.2）
    [[nodiscard]] core::Result<std::vector<WalEntry>> replay() noexcept;

    /// 流式 replay（每条触发一次 callback，callback 返回 false 终止遍历）。
    /// @return Ok() / Err(SERIALIZE_VERSION_MISMATCH / IO)
    [[nodiscard]] core::Result<void> replay_stream(const WalCallback& callback) noexcept;

    // ---------- 维护接口 ----------

    /// 截断：保留 seq >= keep_sequence 的所有记录，删除更早的。
    /// @param keep_sequence 保留的最小 seq（含）
    /// @return Ok() / Err(IO 错误)
    [[nodiscard]] core::Result<void> truncate(std::uint64_t keep_sequence) noexcept;

    /// 检查点截断：保留所有现有记录，截断为新起点（清空文件但保留 schema 头）。
    [[nodiscard]] core::Result<void> truncate_all() noexcept;

    // ---------- 状态查询 ----------

    /// 当前下一个 sequence（已写入的最大 seq + 1）。
    [[nodiscard]] std::uint64_t current_sequence() const noexcept;

    /// 当前文件大小（字节）。
    [[nodiscard]] std::uint64_t size_bytes() const noexcept;

    /// 配置文件路径。
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return config_.path_; }

    /// 当前条目数（仅在 replay 后或 append 后准确）。
    [[nodiscard]] std::uint64_t entry_count() const noexcept { return entry_count_.load(std::memory_order_relaxed); }

private:
    /// 私有构造：由 create() 调用
    explicit Wal(WalConfig config, UniqueFd fd) noexcept;

    /// 写文件头（magic + schema_version + next_seq=0）
    [[nodiscard]] core::Result<void> write_file_header() noexcept;

    /// 读文件头；返回 next_seq（已写入的最大 seq）
    [[nodiscard]] core::Result<std::uint64_t> read_file_header() noexcept;

    /// 写一条 Entry 到 fd
    [[nodiscard]] core::Result<std::uint64_t> write_entry(const WalEntry& entry) noexcept;

    /// 从 fd 当前位置读一条 Entry
    [[nodiscard]] core::Result<WalEntry> read_entry() noexcept;

    /// 扫描文件中所有 entry，返回 (最大 seq, entry 数)。
    /// 用于 read_file_header 中重算 next_seq。
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> scan_entries_for_max_seq() noexcept;

    WalConfig   config_;
    UniqueFd    fd_;
    std::atomic<std::uint64_t> next_seq_{1};     ///< 下一个 sequence（原子，便于无锁读）
    std::atomic<std::uint64_t> size_bytes_{0};   ///< 文件字节数（atomic，简化统计）
    std::atomic<std::uint64_t> entry_count_{0};  ///< 已写入 Entry 数
    mutable std::mutex         mutex_;           ///< 串行化所有 IO
};

}  // namespace udaf::platform::fs

#endif  // UDAF_PLATFORM_FS_WAL_HPP