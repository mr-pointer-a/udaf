// messages.hpp - 能力 C 业务消息结构体
#ifndef UDAF_ABILITY_C_MESSAGES_MESSAGES_HPP
#define UDAF_ABILITY_C_MESSAGES_MESSAGES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace udaf::ability_c::messages {

struct CmdRequest {
    std::string command;
    std::vector<std::string> args;
    std::uint32_t timeout_ms = 5000;
};

struct CmdResult {
    std::int32_t exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    std::uint64_t elapsed_ns = 0;
};

struct FileChunk {
    std::string path;
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> data;
    bool is_last = false;
};

struct FileAck {
    std::string path;
    std::uint64_t offset = 0;
    bool ok = true;
};

struct Heartbeat {
    std::string node_id;
    std::int64_t timestamp_ns = 0;
};

struct NetInterfaceQuery {
    std::string ifname;  // 空 = 全部
};

struct NetInterfaceSet {
    std::string ifname;
    bool up = true;
    std::string address;
};

struct NetInterfaceResult {
    std::string ifname;
    std::string address;
    bool up = true;
    std::int32_t mtu = 0;
};

}  // namespace udaf::ability_c::messages

#endif  // UDAF_ABILITY_C_MESSAGES_MESSAGES_HPP