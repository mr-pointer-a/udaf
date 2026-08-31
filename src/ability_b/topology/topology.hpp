// topology.hpp - 拓扑结构（节点 + 边 + 事务）
//
// 设计要点：
//   - Topology 持有所有节点和边
//   - TopologyTransaction 独立类（评审 M-12）
//   - commit(TopologyTransaction&&) 右值引用防重复提交
//   - begin_transaction 返回 Result<TopologyTransaction>
//   - 不抛异常（CLAUDE.md §3.5）
//
// 设计依据：docs/04-module-design.md §2.3 + docs/03-detailed-design.md §3.3.4

#ifndef UDAF_ABILITY_B_TOPOLOGY_TOPOLOGY_HPP
#define UDAF_ABILITY_B_TOPOLOGY_TOPOLOGY_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"
#include "platform/fs/wal.hpp"

namespace udaf::ability_b::topology {

/// 拓扑节点（运行实例）
struct PeerNode {
    std::string node_id;
    std::string hostname;
    std::string bind_address;
    std::uint16_t bind_port = 0;
    std::vector<std::string> capabilities;
};

/// 拓扑边（节点间连接）
struct PeerEdge {
    std::string from_node;
    std::string to_node;
    std::string protocol;   // "tcp" / "zmq" / "inproc"
};

/// 事务操作类型
enum class TxOp : std::uint8_t { AddNode, RemoveNode, AddEdge, RemoveEdge };

/// 事务中的一项操作
struct TxItem {
    TxOp op;
    std::string a;
    std::string b;
    std::string tag;
};

/// 拓扑事务（独立类，评审 M-12）
class TopologyTransaction {
public:
    TopologyTransaction() = default;

    // Rule of Five
    TopologyTransaction(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(const TopologyTransaction&) = delete;
    TopologyTransaction(TopologyTransaction&&) noexcept = default;
    TopologyTransaction& operator=(TopologyTransaction&&) noexcept = default;

    /// 添加节点
    TopologyTransaction& add_node(PeerNode n) noexcept;
    /// 移除节点
    TopologyTransaction& remove_node(std::string node_id) noexcept;
    /// 添加边
    TopologyTransaction& add_edge(PeerEdge e) noexcept;
    /// 移除边
    TopologyTransaction& remove_edge(std::string from, std::string to) noexcept;

    /// 已加入的操作数
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

    /// 仅内部访问
    [[nodiscard]] const std::vector<TxItem>& items() const noexcept { return items_; }

private:
    std::vector<TxItem> items_;
    friend class Topology;
};

/// 拓扑主类
class Topology {
public:
    explicit Topology(std::shared_ptr<platform::fs::Wal> wal = nullptr) noexcept
        : wal_(std::move(wal)) {}

    /// 开始一个新事务
    [[nodiscard]] static core::Result<TopologyTransaction> begin_transaction() noexcept;

    /// 提交事务（消耗 tx，右值引用防重复提交）
    /// @return Err(INVALID_ARG) 当 tx 已为空
    [[nodiscard]] core::Result<void>
    commit(TopologyTransaction&& tx) noexcept;

    /// 节点数
    [[nodiscard]] std::size_t node_count() const noexcept;

    /// 边数
    [[nodiscard]] std::size_t edge_count() const noexcept;

    /// 查询节点
    [[nodiscard]] core::Result<PeerNode>
    get_node(std::string_view node_id) const noexcept;

    /// 全部节点快照
    [[nodiscard]] std::vector<PeerNode> nodes() const noexcept;

    /// 全部边快照
    [[nodiscard]] std::vector<PeerEdge> edges() const noexcept;

    /// 检测是否有环（DFS）
    [[nodiscard]] bool has_cycle() const noexcept;

    /// 清空（仅测试）
    void clear() noexcept;

private:
    void write_to_wal(const TxItem& it) noexcept;

    mutable std::mutex mtx_;
    std::unordered_map<std::string, PeerNode> nodes_;
    std::unordered_set<std::string> edges_;  // "from|to" 形式

    std::shared_ptr<platform::fs::Wal> wal_;
};

}  // namespace udaf::ability_b::topology

#endif  // UDAF_ABILITY_B_TOPOLOGY_TOPOLOGY_HPP