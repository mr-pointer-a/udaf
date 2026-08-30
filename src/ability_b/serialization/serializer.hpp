// serializer.hpp - 序列化器（编码/解码 + schema 版本校验）
//
// 设计要点（评审 C-3）：
//   - SerializerBase 多态 + Serializer<T> 模板包装
//   - encode 返回 Result<vector<uint8_t>>
//   - decode 返回 Result<shared_ptr<const T>>（不抛异常）
//   - accepts_type(string_view) 校验类型字符串触发 SERIALIZE_TYPE_MISMATCH
//   - 4 字节 schema_version（网络字节序）+ 类型字符串 + payload
//
// 设计依据：docs/04-module-design.md §2.7 + docs/03-detailed-design.md §3.3.7
// + docs/adr/ADR-002-serialization.md

#ifndef UDAF_ABILITY_B_SERIALIZATION_SERIALIZER_HPP
#define UDAF_ABILITY_B_SERIALIZATION_SERIALIZER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <typeindex>
#include <vector>

#include "core/error_code.hpp"
#include "core/result.hpp"

namespace udaf::ability_b::serialization {

/// 帧头：4 字节 magic "UDAF" + 4 字节 schema_version (BE) + 1 字节 type_len + type
constexpr std::uint32_t kCodecMagic = 0x55444146;  // "UDAF"
constexpr std::size_t kHeaderSize  = 4 + 4 + 1;    // magic + version + type_len

/// 序列化器抽象基类
class SerializerBase {
public:
    virtual ~SerializerBase() = default;

    /// 当前 serializer 关联的类型名（如 "cmd_request"）
    [[nodiscard]] virtual std::string type_name() const noexcept = 0;

    /// schema 版本号
    [[nodiscard]] virtual std::uint32_t schema_version() const noexcept = 0;

    /// 将 payload（已经过类型特定编码）打包为完整帧
    /// @param payload 类型特定的编码后字节流（保留参数，便于未来扩展 trailer）
    /// @return 完整帧（header + payload）
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    encode(std::span<const std::uint8_t> payload) const noexcept;

    /// 从完整帧中提取 payload
    /// @return SERIALIZE_VERSION_MISMATCH / SERIALIZE_TYPE_MISMATCH / SERIALIZE_DECODE_FAILED
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    decode_payload(std::span<const std::uint8_t> frame) const noexcept;

    /// 是否接受给定类型字符串
    [[nodiscard]] virtual bool accepts_type(std::string_view type) const noexcept = 0;

    /// 类型特定 payload 的编码（子类实现）
    [[nodiscard]] virtual
    core::Result<std::vector<std::uint8_t>> encode_payload() const noexcept = 0;

    /// 类型特定 payload 的解码（子类实现）
    [[nodiscard]] virtual
    core::Result<void> decode_payload_inplace(
        std::span<const std::uint8_t> payload) noexcept = 0;
};

/// 序列化器模板包装（CRTP + 实例缓存 payload 副本）
template <typename T, typename Derived>
class Serializer : public SerializerBase {
public:
    /// 用已有 payload 反序列化（拷贝）
    explicit Serializer(std::vector<std::uint8_t> payload) noexcept
        : cached_payload_(std::move(payload)) {}

    /// 空构造（用于编码）
    Serializer() = default;

    /// 类型名（从 typeid 派生）
    [[nodiscard]] std::string type_name() const noexcept override {
        return typeid(T).name();
    }

    /// 子类必须覆盖 schema_version
    [[nodiscard]] std::uint32_t schema_version() const noexcept override = 0;

    [[nodiscard]] bool accepts_type(std::string_view type) const noexcept override {
        return type == type_name();
    }

    /// 编码：调用子类的 encode_payload()
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    encode_payload() const noexcept override {
        return static_cast<Derived*>(this)->encode_payload_impl();
    }

    /// 解码：调用子类的 decode_payload_inplace()
    [[nodiscard]] core::Result<void>
    decode_payload_inplace(std::span<const std::uint8_t> payload) noexcept override {
        cached_payload_.assign(payload.begin(), payload.end());
        return static_cast<Derived*>(this)->decode_payload_inplace_impl();
    }

    /// 获取已解码的 payload 副本
    [[nodiscard]] const std::vector<std::uint8_t>& payload() const noexcept {
        return cached_payload_;
    }

private:
    std::vector<std::uint8_t> cached_payload_;
};

}  // namespace udaf::ability_b::serialization

#endif  // UDAF_ABILITY_B_SERIALIZATION_SERIALIZER_HPP