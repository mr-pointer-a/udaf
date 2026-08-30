// keystore.cpp - PSK 文件持久化
#include "keystore.hpp"

#include "core/log/logger.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/rand.h>

#include <cstring>
#include <fstream>

namespace udaf::crypto {

namespace {

constexpr std::uint32_t kPskFileMagic = 0x50534B01;  // "PSK\x01"
constexpr std::size_t kPskLen = 32;

}  // namespace

core::Result<SecretKey> generate_psk() noexcept {
    SecretKey k(kPskLen);
    if (RAND_bytes(k.data(), static_cast<int>(k.size())) != 1) {
        return core::Result<SecretKey>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<SecretKey>::ok(std::move(k));
}

core::Result<SecretKey>
load_psk_from_file(const std::filesystem::path& path) noexcept {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return core::Result<SecretKey>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
    }

    std::uint32_t magic = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != kPskFileMagic) {
        return core::Result<SecretKey>::err(core::ErrorCode::SERIALIZE_VERSION_MISMATCH);
    }

    SecretKey k(kPskLen);
    ifs.read(reinterpret_cast<char*>(k.data()), static_cast<std::streamsize>(k.size()));
    if (ifs.gcount() != static_cast<std::streamsize>(k.size())) {
        return core::Result<SecretKey>::err(core::ErrorCode::PROTOCOL_TRUNCATED_BUFFER);
    }
    return core::Result<SecretKey>::ok(std::move(k));
}

core::Result<void>
save_psk_to_file(const std::filesystem::path& path, std::span<const std::uint8_t> psk) noexcept {
    if (psk.size() != kPskLen) {
        return core::Result<void>::err(core::ErrorCode::INVALID_ARG);
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    // 写入临时文件 + rename（原子性）
    const auto tmp = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return core::Result<void>::err(core::ErrorCode::BIZ_FILE_NOT_FOUND);
        }
        const std::uint32_t magic = kPskFileMagic;
        ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        ofs.write(reinterpret_cast<const char*>(psk.data()),
                  static_cast<std::streamsize>(psk.size()));
        if (!ofs.good()) {
            return core::Result<void>::err(core::ErrorCode::INTERNAL);
        }
    }
    ::chmod(tmp.c_str(), 0640);
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        return core::Result<void>::err(core::ErrorCode::INTERNAL);
    }
    return core::Result<void>::ok();
}

}  // namespace udaf::crypto