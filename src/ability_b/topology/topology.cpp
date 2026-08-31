// topology.cpp - Topology 实现
#include "topology.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace udaf::ability_b::topology {

// ---------- TopologyTransaction ----------

TopologyTransaction& TopologyTransaction::add_node(PeerNode n) noexcept {
    items_.push_back({TxOp::AddNode, std::move(n.node_id),
                       std::move(n.hostname), ""});
    return *this;
}

TopologyTransaction& TopologyTransaction::remove_node(std::string node_id) noexcept {
    items_.push_back({TxOp::RemoveNode, std::move(node_id), "", ""});
    return *this;
}

TopologyTransaction& TopologyTransaction::add_edge(PeerEdge e) noexcept {
    items_.push_back({TxOp::AddEdge, std::move(e.from_node),
                       std::move(e.to_node), std::move(e.protocol)});
    return *this;
}

TopologyTransaction& TopologyTransaction::remove_edge(std::string from,
                                                     std::string to) noexcept {
    items_.push_back({TxOp::RemoveEdge, std::move(from), std::move(to), ""});
    return *this;
}

// ---------- Topology ----------

core::Result<TopologyTransaction> Topology::begin_transaction() noexcept {
    return core::Result<TopologyTransaction>::ok(TopologyTransaction{});
}

static std::string edge_key(const std::string& a, const std::string& b) {
    return a + "|" + b;
}

void Topology::write_to_wal(const TxItem& /*it*/) noexcept {
    // 简化：真实实现应该用 Wal::append（action, payload）
    // 此处依赖外部注入 wal_
}

core::Result<void> Topology::commit(TopologyTransaction&& tx) noexcept {  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    // 评审 M-12：右值引用语义上"消费"事务，防止重复提交；实际数据已复制到 nodes_/edges_
    if (tx.empty()) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    std::unique_lock<std::mutex> lock(mtx_);
    for (const auto& it : tx.items()) {
        switch (it.op) {
            case TxOp::AddNode: {
                PeerNode n;
                n.node_id = it.a;
                nodes_[it.a] = std::move(n);
                break;
            }
            case TxOp::RemoveNode: {
                nodes_.erase(it.a);
                // 删除所有相关边
                for (auto it2 = edges_.begin(); it2 != edges_.end(); ) {
                    auto p = it2->find('|');
                    if (p != std::string::npos) {
                        std::string a = it2->substr(0, p);
                        std::string b = it2->substr(p + 1);
                        if (a == it.a || b == it.a) {
                            it2 = edges_.erase(it2);
                            continue;
                        }
                    }
                    ++it2;
                }
                break;
            }
            case TxOp::AddEdge: {
                edges_.insert(edge_key(it.a, it.b));
                break;
            }
            case TxOp::RemoveEdge: {
                edges_.erase(edge_key(it.a, it.b));
                break;
            }
        }
        write_to_wal(it);
    }
    return core::Result<void>::ok();
}

std::size_t Topology::node_count() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return nodes_.size();
}

std::size_t Topology::edge_count() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return edges_.size();
}

core::Result<PeerNode>
Topology::get_node(std::string_view node_id) const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = nodes_.find(std::string(node_id));
    if (it == nodes_.end()) {
        return core::Result<PeerNode>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
    }
    return core::Result<PeerNode>::ok(it->second);
}

std::vector<PeerNode> Topology::nodes() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<PeerNode> out;
    out.reserve(nodes_.size());
    for (const auto& [k, v] : nodes_) {
        (void)k;
        out.push_back(v);
    }
    return out;
}

std::vector<PeerEdge> Topology::edges() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<PeerEdge> out;
    out.reserve(edges_.size());
    for (const auto& k : edges_) {
        auto p = k.find('|');
        if (p == std::string::npos) continue;
        out.push_back(PeerEdge{k.substr(0, p), k.substr(p + 1), ""});
    }
    return out;
}

bool Topology::has_cycle() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    // DFS 检测环：仅在节点数 >= 2 时可能
    if (nodes_.size() < 2) return false;

    // 构建邻接表
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& e : edges_) {
        auto p = e.find('|');
        if (p == std::string::npos) continue;
        adj[e.substr(0, p)].push_back(e.substr(p + 1));
    }

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> stack;
    bool has = false;
    std::function<bool(const std::string&)> dfs = [&](const std::string& u) {
        if (stack.contains(u)) { has = true; return true; }
        if (visited.contains(u)) return false;
        visited.insert(u);
        stack.insert(u);
        if (adj.contains(u)) {
            for (auto& v : adj[u]) {
                if (dfs(v)) return true;
            }
        }
        stack.erase(u);
        return false;
    };
    for (const auto& [k, _] : nodes_) {
        (void)_;
        if (dfs(k)) break;
    }
    return has;
}

void Topology::clear() noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    nodes_.clear();
    edges_.clear();
}

}  // namespace udaf::ability_b::topology