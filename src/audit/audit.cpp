// audit.cpp - 实现（OpenSSL SHA-512 + 文件 append）
//
// 文件格式（ADR-006 §2.2 hash chain 完整实装）：
//   第 0 行：GENESIS|<创世 hash 64-hex>
//   第 N 行（N≥1）：seq|action|actor|target|ts|prev_hash|params_hash|json
//
// 链式关系：
//   prev_hash(1) = genesis
//   prev_hash(N+1) = SHA-512(prev_hash(N) || params_hash(N) || seq(N) || action(N))
//
// verify_chain：从磁盘读出 GENESIS 行，逐条重算 prev_hash 并比对文件中存储的 prev_hash；
// 任一不匹配 → 返回 Ok(false)。
#include "audit.hpp"

#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace udaf::audit {

namespace {

std::string sha512_hex(const std::string& data) noexcept {
    unsigned char out[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<const unsigned char*>(data.data()),
           data.size(), out);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : out) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string random_hex(std::size_t bytes) noexcept {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes; ++i) {
        oss << std::setw(2) << static_cast<int>(rng() & 0xff);
    }
    return oss.str();
}

// 解析一行审计记录到 AuditEvent；格式：seq|action|actor|target|ts|prev|params_h|json
// 注：actor/target/json 可能含 '|'，但当前实现约束为不含（仅 ASCII 字母数字 + JSON 字符串）；
// 若 json 内出现 '|' 字符（不应发生），按最后一个 '|' 之后整段作为 json，前面 7 段固定。
bool parse_event_line(const std::string& line, AuditEvent& ev,
                      std::string& json_tail) noexcept {
    // 固定 7 个 '|' 分隔符 + json 尾段
    std::array<std::size_t, 7> cuts{};
    std::size_t k = 0;
    for (std::size_t i = 0; i < line.size() && k < 7; ++i) {
        if (line[i] == '|') cuts[k++] = i;
    }
    if (k != 7) return false;
    try {
        ev.sequence     = std::stoull(line.substr(0, cuts[0]));
        std::string act = line.substr(cuts[0] + 1, cuts[1] - cuts[0] - 1);
        ev.actor        = line.substr(cuts[1] + 1, cuts[2] - cuts[1] - 1);
        ev.target       = line.substr(cuts[2] + 1, cuts[3] - cuts[2] - 1);
        ev.timestamp_ns = std::stoll(line.substr(cuts[3] + 1, cuts[4] - cuts[3] - 1));
        ev.prev_hash    = line.substr(cuts[4] + 1, cuts[5] - cuts[4] - 1);
        ev.params_hash  = line.substr(cuts[5] + 1, cuts[6] - cuts[5] - 1);
        json_tail       = line.substr(cuts[6] + 1);
        // 反查 action_name → ActionType
        ev.action = ActionType::NodeHeartbeat;  // 默认
        for (std::uint16_t v = 1; v <= 19; ++v) {
            if (act == action_name(static_cast<ActionType>(v))) {
                ev.action = static_cast<ActionType>(v);
                break;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace

AuditLogger::AuditLogger(std::string path) : path_(std::move(path)) {
    // 若文件已存在（含 GENESIS 行），沿用其创世 hash + 重建 prev_hash_ 末端 + seq_；
    // 否则计算新创世 hash 并写入文件第 0 行。
    std::ifstream in(path_);
    std::string first;
    if (in.is_open() && std::getline(in, first)) {
        if (first.compare(0, 8, "GENESIS|") == 0) {
            prev_hash_ = first.substr(8);
            std::string line;
            std::string cur_prev = prev_hash_;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                AuditEvent ev{};
                std::string json_tail;
                if (!parse_event_line(line, ev, json_tail)) break;
                if (ev.prev_hash != cur_prev) break;  // 链断裂 → 不回填 seq
                seq_ = ev.sequence;  // 记录最大 seq
                std::ostringstream next;
                next << cur_prev << '|' << ev.params_hash << '|'
                     << ev.sequence << '|' << static_cast<int>(ev.action);
                cur_prev = sha512_hex(next.str());
            }
            prev_hash_ = cur_prev;  // 末端 hash（下一条 append 用）
            return;
        }
    }
    // 创世 hash：随机 + 时间（ADR-006 §2.2 三源混合降级为随机+时间）
    std::ostringstream seed;
    seed << "udaf-bootstrap-" << random_hex(16) << "-" << now_ns();
    prev_hash_ = sha512_hex(seed.str());

    std::ofstream out(path_, std::ios::app);
    if (out.is_open()) {
        out << "GENESIS|" << prev_hash_ << '\n';
    }
}

std::string AuditLogger::compute_params_hash(const std::string& json) noexcept {
    return sha512_hex(json);
}

core::Result<std::uint64_t>
AuditLogger::append(ActionType action, std::string actor, std::string target,
                    std::string params_json) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    std::uint64_t seq = ++seq_;
    auto params_hash = compute_params_hash(params_json);

    AuditEvent ev;
    ev.sequence     = seq;
    ev.action       = action;
    ev.actor        = std::move(actor);
    ev.target       = std::move(target);
    ev.params_json  = std::move(params_json);
    ev.timestamp_ns = now_ns();
    ev.prev_hash    = prev_hash_;
    ev.params_hash  = params_hash;

    // 更新 prev_hash = SHA-512(prev_hash || params_hash || seq || action)
    std::ostringstream next;
    next << prev_hash_ << '|' << params_hash << '|' << seq << '|'
         << static_cast<int>(action);
    prev_hash_ = sha512_hex(next.str());

    std::ofstream out(path_, std::ios::app);
    if (!out.is_open()) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }
    // line-format：seq|action|actor|target|ts|prev|params_h|json
    out << ev.sequence << '|' << action_name(ev.action) << '|'
        << ev.actor << '|' << ev.target << '|'
        << ev.timestamp_ns << '|' << ev.prev_hash << '|'
        << ev.params_hash << '|' << ev.params_json << '\n';
    if (!out.good()) {
        return core::Result<std::uint64_t>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<std::uint64_t>::ok(seq);
}

core::Result<bool> AuditLogger::verify_chain() noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ifstream in(path_);
    if (!in.is_open()) {
        return core::Result<bool>::err(core::ErrorCode::INTERNAL);
    }
    std::string line;
    if (!std::getline(in, line)) {
        return core::Result<bool>::err(core::ErrorCode::INTERNAL);
    }
    if (line.compare(0, 8, "GENESIS|") != 0) {
        // 旧格式（无 GENESIS 行）→ 不可校验
        return core::Result<bool>::err(core::ErrorCode::PROTOCOL_VERSION_MISMATCH);
    }
    std::string expected_prev = line.substr(8);

    // 逐条重算 prev_hash 并比对
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        AuditEvent ev{};
        std::string json_tail;
        if (!parse_event_line(line, ev, json_tail)) {
            return core::Result<bool>::ok(false);  // 格式损坏
        }
        if (ev.prev_hash != expected_prev) {
            return core::Result<bool>::ok(false);  // 链断裂
        }
        // 验证 params_hash 与 json 一致
        if (compute_params_hash(json_tail) != ev.params_hash) {
            return core::Result<bool>::ok(false);
        }
        // 推进 expected_prev
        std::ostringstream next;
        next << expected_prev << '|' << ev.params_hash << '|'
             << ev.sequence << '|' << static_cast<int>(ev.action);
        expected_prev = sha512_hex(next.str());
    }
    return core::Result<bool>::ok(true);
}

std::uint64_t AuditLogger::sequence() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return seq_;
}

}  // namespace udaf::audit