// nodes.cpp - 业务节点实现
#include "nodes.hpp"

#include "ability_b/port/port.hpp"

#include <chrono>
#include <utility>

namespace udaf::ability_c::nodes {

// ---------------- CmdExecNode ----------------

CmdExecNode::CmdExecNode()
    : Node("cmd_exec"),
      in_cmd_("cmd_in", 64),
      out_result_(ability_b::port::PortInfo{"cmd_out",
                  std::type_index(typeid(messages::CmdResult)), 1, false},
                  nullptr) {
    inputs_.push_back(in_cmd_.info());
    outputs_.push_back(out_result_.info());
}

CmdExecNode::~CmdExecNode() {
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    // 析构阶段调用 stop() 是有意为之（基类析构前必须先停止 worker_ 线程）。
    // 虚分派在此处按值进行是安全的，因为 CmdExecNode 已是最末派生类。
    (void)stop();
}

void CmdExecNode::set_allowed_executables(std::vector<std::string> a) noexcept {
    allowed_ = std::move(a);
}

core::Result<void>
CmdExecNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    worker_ = std::thread([this] { worker(); });
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::stop() noexcept {
    if (!running_.exchange(false)) {
        set_state(ability_b::node::LifecycleState::Stopped);
        return core::Result<void>::ok();
    }
    if (worker_.joinable()) worker_.join();
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> CmdExecNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

void CmdExecNode::worker() noexcept {
    while (running_.load()) {
        try {
            auto r = in_cmd_.recv(100);
            if (r.is_err()) continue;
            const auto& req = r.value();
            executor::ProcessExecutor::Options opts;
            opts.executable = req.command;
            opts.args = req.args;
            opts.allowed_executables = allowed_;
            auto er = executor::ProcessExecutor::execute(opts);
            messages::CmdResult res;
            if (er.is_ok()) {
                res.exit_code = er.value().exit_code;
                res.stdout_text = std::move(er.value().stdout_text);
                res.stderr_text = std::move(er.value().stderr_text);
                res.elapsed_ns = er.value().elapsed_ns;
            } else {
                res.exit_code = -1;
                res.stderr_text = "exec failed";
            }
            (void)out_result_.try_send(std::move(res));
        } catch (const std::exception& /*e*/) {
            // NOLINT(bugprone-empty-catch)
            // 任何 STL/系统异常均吞掉（worker 线程不应终止）
            (void)0;
        }
    }
}

// ---------------- HeartbeatNode ----------------

HeartbeatNode::HeartbeatNode()
    : Node("heartbeat"),
      in_hb_("hb_in", 1024),
      out_hb_(ability_b::port::PortInfo{"hb_out",
                std::type_index(typeid(messages::Heartbeat)), 1, false},
                nullptr) {
    inputs_.push_back(in_hb_.info());
    outputs_.push_back(out_hb_.info());
}

HeartbeatNode::~HeartbeatNode() = default;

core::Result<void>
HeartbeatNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::start() noexcept {
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::stop() noexcept {
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> HeartbeatNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

// ---------------- FileXferNode ----------------

FileXferNode::FileXferNode()
    : Node("file_xfer"),
      in_chunk_("chunk_in", 16),
      out_ack_(ability_b::port::PortInfo{"ack_out",
                  std::type_index(typeid(messages::FileAck)), 1, false},
                  nullptr) {
    inputs_.push_back(in_chunk_.info());
    outputs_.push_back(out_ack_.info());
}

FileXferNode::~FileXferNode() {
    (void)stop();
}

void FileXferNode::set_allowed_paths(std::vector<std::string> roots) noexcept {
    allowed_roots_ = std::move(roots);
}

core::Result<void>
FileXferNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> FileXferNode::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    worker_ = std::thread([this] { worker(); });
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> FileXferNode::stop() noexcept {
    if (!running_.exchange(false)) {
        set_state(ability_b::node::LifecycleState::Stopped);
        return core::Result<void>::ok();
    }
    if (worker_.joinable()) worker_.join();
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> FileXferNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

bool FileXferNode::path_allowed(const std::string& p) const noexcept {
    if (p.empty()) return false;
    // 路径穿越检查（评审 C-2）：禁止 ../ 跳出
    if (p.find("..") != std::string::npos) return false;
    // 未限制则放行（开发态）；生产应设置 allowed_paths
    if (allowed_roots_.empty()) return true;
    // 路径必须以某个允许的根目录开头（绝对/相对均允许）
    for (const auto& root : allowed_roots_) {
        if (p.size() >= root.size() &&
            p.compare(0, root.size(), root) == 0) {
            return true;
        }
    }
    return false;
}

std::int64_t FileXferNode::write_block(
    const std::string& path, std::uint64_t offset,
    const std::vector<std::uint8_t>& data) noexcept {
    if (!path_allowed(path)) return -1;
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) {
        // 文件不存在则创建
        fp = std::fopen(path.c_str(), "w+b");
        if (!fp) return -1;
    }
    if (std::fseek(fp, static_cast<long>(offset), SEEK_SET) != 0) {
        std::fclose(fp);
        return -1;
    }
    std::int64_t n = (data.empty())
        ? 0
        : static_cast<std::int64_t>(std::fwrite(data.data(), 1, data.size(), fp));
    std::fclose(fp);
    return n;
}

void FileXferNode::worker() noexcept {
    while (running_.load()) {
        try {
            auto r = in_chunk_.recv(100);
            if (r.is_err()) continue;
            const auto& chunk = r.value();
            messages::FileAck ack;
            ack.path = chunk.path;
            ack.offset = chunk.offset;
            ack.ok = true;
            if (!path_allowed(chunk.path)) {
                ack.ok = false;
            } else {
                auto n = write_block(chunk.path, chunk.offset, chunk.data);
                if (n < 0) ack.ok = false;
            }
            (void)out_ack_.try_send(std::move(ack));
        } catch (...) {
            // worker 线程不应终止
        }
    }
}

// ---------------- NetInfoNode ----------------

NetInfoNode::NetInfoNode()
    : Node("net_info"),
      in_query_("query_in", 32),
      in_set_("set_in", 16),
      out_result_(ability_b::port::PortInfo{"result_out",
                     std::type_index(typeid(messages::NetInterfaceResult)), 1, false},
                     nullptr) {
    inputs_.push_back(in_query_.info());
    inputs_.push_back(in_set_.info());
    outputs_.push_back(out_result_.info());
}

NetInfoNode::~NetInfoNode() {
    (void)stop();
}

core::Result<void>
NetInfoNode::init(const ability_b::node::NodeConfig& /*cfg*/) noexcept {
    set_state(ability_b::node::LifecycleState::Init);
    return core::Result<void>::ok();
}

core::Result<void> NetInfoNode::start() noexcept {
    if (running_.exchange(true)) {
        return core::Result<void>::err(core::ErrorCode::RESOURCE_BUSY);
    }
    worker_ = std::thread([this] {
        while (running_.load()) {
            try {
                auto qr = in_query_.try_recv();
                if (qr.is_ok()) {
                    auto res = query_one(qr.value().ifname);
                    (void)out_result_.try_send(std::move(res));
                    continue;
                }
                auto sr = in_set_.try_recv();
                if (sr.is_ok()) {
                    auto res = apply_set(sr.value());
                    (void)out_result_.try_send(std::move(res));
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } catch (...) {
                // 不应终止
            }
        }
    });
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

core::Result<void> NetInfoNode::stop() noexcept {
    if (!running_.exchange(false)) {
        set_state(ability_b::node::LifecycleState::Stopped);
        return core::Result<void>::ok();
    }
    if (worker_.joinable()) worker_.join();
    set_state(ability_b::node::LifecycleState::Stopped);
    return core::Result<void>::ok();
}

core::Result<void> NetInfoNode::reload() noexcept {
    set_state(ability_b::node::LifecycleState::Reloading);
    set_state(ability_b::node::LifecycleState::Running);
    return core::Result<void>::ok();
}

messages::NetInterfaceResult
NetInfoNode::query_one(const std::string& ifname) noexcept {
    messages::NetInterfaceResult r;
    r.ifname = ifname;
    // 简化实现：通过 /sys/class/net/<name>/operstate 读 up/down
    // 真实环境应走 netlink socket
    if (ifname.empty()) {
        r.address = "";
        r.up = false;
        r.mtu = 0;
        return r;
    }
    std::string path = "/sys/class/net/" + ifname + "/operstate";
    std::FILE* fp = std::fopen(path.c_str(), "r");
    if (!fp) {
        r.up = false;
        return r;
    }
    char buf[16] = {0};
    (void)std::fread(buf, 1, sizeof(buf) - 1, fp);
    std::fclose(fp);
    std::string state = buf;
    // 去掉换行
    while (!state.empty() && (state.back() == '\n' || state.back() == '\r')) {
        state.pop_back();
    }
    r.up = (state == "up");
    return r;
}

messages::NetInterfaceResult
NetInfoNode::apply_set(const messages::NetInterfaceSet& s) noexcept {
    // 简化：仅记录，不真实操作（真实环境走 netlink/ioctl）
    // 此处返回 applied 后的状态
    messages::NetInterfaceResult r;
    r.ifname = s.ifname;
    r.up = s.up;
    r.address = s.address;
    return r;
}

}  // namespace udaf::ability_c::nodes