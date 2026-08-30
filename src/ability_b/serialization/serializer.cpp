// serializer.cpp - SerializerBase 编解码实现
#include "serializer.hpp"

#include <algorithm>
#include <cstring>

namespace udaf::ability_b::serialization {

namespace {

inline void write_u32_be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

inline std::uint32_t read_u32_be(std::span<const std::uint8_t> buf) {
    return (static_cast<std::uint32_t>(buf[0]) << 24) |
           (static_cast<std::uint32_t>(buf[1]) << 16) |
           (static_cast<std::uint32_t>(buf[2]) << 8) |
           static_cast<std::uint32_t>(buf[3]);
}

}  // namespace

core::Result<std::vector<std::uint8_t>>
SerializerBase::encode(std::span<const std::uint8_t> /*payload*/) const noexcept {
    auto payload_result = const_cast<SerializerBase*>(this)->encode_payload();
    if (payload_result.is_err()) {
        return core::Result<std::vector<std::uint8_t>>::err(payload_result.error());
    }
    const auto& p = payload_result.value();

    std::string tn = type_name();
    if (tn.size() > 255) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_TYPE_MISMATCH);
    }

    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + tn.size() + p.size());
    write_u32_be(out, kCodecMagic);
    write_u32_be(out, schema_version());
    out.push_back(static_cast<std::uint8_t>(tn.size()));
    out.insert(out.end(), tn.begin(), tn.end());
    out.insert(out.end(), p.begin(), p.end());
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<std::vector<std::uint8_t>>
SerializerBase::decode_payload(std::span<const std::uint8_t> frame) const noexcept {
    if (frame.size() < kHeaderSize) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }
    const std::uint32_t magic = read_u32_be(frame.subspan(0, 4));
    if (magic != kCodecMagic) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }
    const std::uint32_t version = read_u32_be(frame.subspan(4, 4));
    if (version != schema_version()) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }
    const auto tlen = static_cast<std::size_t>(frame[8]);
    if (frame.size() < kHeaderSize + tlen) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_DECODE_FAILED);
    }
    std::string_view tn(reinterpret_cast<const char*>(frame.data() + kHeaderSize), tlen);
    if (!accepts_type(tn)) {
        return core::Result<std::vector<std::uint8_t>>::err(
            core::ErrorCode::SERIALIZE_TYPE_MISMATCH);
    }
    std::vector<std::uint8_t> payload(
        frame.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + tlen),
        frame.end());
    auto decoded = const_cast<SerializerBase*>(this)->decode_payload_inplace(payload);
    if (decoded.is_err()) {
        return core::Result<std::vector<std::uint8_t>>::err(decoded.error());
    }
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(payload));
}

}  // namespace udaf::ability_b::serialization