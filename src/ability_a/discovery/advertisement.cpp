// advertisement.cpp - AdvertisementPayload 序列化
#include "advertisement.hpp"

#include <cstdint>
#include <cstring>
#include <span>

namespace udaf::ability_a::discovery {

namespace {

inline void write_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

inline std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

inline void write_str(std::vector<std::uint8_t>& out, const std::string& s) {
    const auto len = static_cast<std::uint16_t>(s.size());
    write_u16(out, len);
    out.insert(out.end(), s.begin(), s.end());
}

}  // namespace

std::vector<std::uint8_t>
serialize_payload(const AdvertisementPayload& p) noexcept {
    std::vector<std::uint8_t> out;
    write_str(out, p.node_id);
    write_str(out, p.hostname);
    write_str(out, p.bind_address);
    write_u16(out, p.bind_port);
    write_u16(out, static_cast<std::uint16_t>(p.services.size()));
    for (const auto& s : p.services) write_str(out, s);
    return out;
}

AdvertisementPayload
parse_payload(std::span<const std::uint8_t> buf) noexcept {
    AdvertisementPayload p;
    std::size_t off = 0;
    auto read_str = [&](std::string& s) -> bool {
        if (off + 2 > buf.size()) return false;
        const auto len = read_u16(buf.data() + off);
        off += 2;
        if (off + len > buf.size()) return false;
        s.assign(reinterpret_cast<const char*>(buf.data() + off), len);
        off += len;
        return true;
    };
    if (!read_str(p.node_id))      return p;
    if (!read_str(p.hostname))     return p;
    if (!read_str(p.bind_address)) return p;
    if (off + 2 > buf.size())      return p;
    p.bind_port = read_u16(buf.data() + off);
    off += 2;
    if (off + 2 > buf.size())      return p;
    const auto n_services = read_u16(buf.data() + off);
    off += 2;
    p.services.reserve(n_services);
    for (std::uint16_t i = 0; i < n_services; ++i) {
        std::string s;
        if (!read_str(s)) break;
        p.services.push_back(std::move(s));
    }
    return p;
}

}  // namespace udaf::ability_a::discovery