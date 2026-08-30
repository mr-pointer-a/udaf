// advertisement.hpp - 发现协议数据包定义
//
// 设计要点：
//   - AdvertisementPayload：nonce + 32B MAC + 重放防护（review v2.1）
//   - 4 字节 magic "UDAF" + 4 字节 version + 8 字节 nonce + 32 字节 HMAC + payload

#ifndef UDAF_ABILITY_A_DISCOVERY_ADVERTISEMENT_HPP
#define UDAF_ABILITY_A_DISCOVERY_ADVERTISEMENT_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace udaf::ability_a::discovery {

constexpr std::uint32_t kDiscoveryMagic   = 0x44434144;  // "DCAD"
constexpr std::uint32_t kDiscoveryVersion = 1;
constexpr std::size_t   kDiscoveryHeaderSize = 4 + 4 + 8 + 32;  // magic + ver + nonce + mac

/// 单条广播内容（明文部分）
struct AdvertisementPayload {
    std::string node_id;
    std::string hostname;
    std::string bind_address;
    std::uint16_t bind_port = 0;
    std::vector<std::string> services;
};

/// 序列化为字节流（不含 nonce/MAC，由 Advertiser 负责添加）
[[nodiscard]] std::vector<std::uint8_t>
serialize_payload(const AdvertisementPayload& p) noexcept;

/// 解析（不含 nonce/MAC）
[[nodiscard]] AdvertisementPayload
parse_payload(std::span<const std::uint8_t> buf) noexcept;

}  // namespace udaf::ability_a::discovery

#endif  // UDAF_ABILITY_A_DISCOVERY_ADVERTISEMENT_HPP