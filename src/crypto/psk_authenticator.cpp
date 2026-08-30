// psk_authenticator.cpp - PskAuthenticator 实现
#include "psk_authenticator.hpp"

#include "psk.hpp"

#include <cstring>
#include <utility>

namespace udaf::crypto {

namespace {

/// AuthRequest/AuthResponse 二进制序列化（简单做法，便于测试与跨平台）
std::vector<std::uint8_t> serialize_request(const AuthRequest& req) {
    std::vector<std::uint8_t> out;
    const auto put = [&](const std::vector<std::uint8_t>& v) {
        out.insert(out.end(), v.begin(), v.end());
    };
    put(req.client_random);  // 32B
    put(req.salt);           // 32B
    out.push_back(static_cast<std::uint8_t>(req.identity.size()));
    out.insert(out.end(), req.identity.begin(), req.identity.end());
    return out;
}

AuthRequest deserialize_request(std::span<const std::uint8_t> buf) {
    AuthRequest req;
    if (buf.size() < 65) return req;
    req.client_random.assign(buf.begin(), buf.begin() + 32);
    req.salt.assign(buf.begin() + 32, buf.begin() + 64);
    const auto id_len = static_cast<std::size_t>(buf[64]);
    if (buf.size() < 65 + id_len) {
        req.client_random.clear();
        req.salt.clear();
        return req;
    }
    req.identity.assign(buf.begin() + 65, buf.begin() + 65 + id_len);
    return req;
}

std::vector<std::uint8_t> serialize_response(const AuthResponse& resp) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), resp.server_random.begin(), resp.server_random.end());
    out.insert(out.end(), resp.salt.begin(), resp.salt.end());
    out.insert(out.end(), resp.nonce.begin(), resp.nonce.end());
    out.insert(out.end(), resp.mac.begin(), resp.mac.end());
    const auto len = static_cast<std::uint32_t>(resp.encrypted_session_key.size());
    out.insert(out.end(),
               reinterpret_cast<const std::uint8_t*>(&len),
               reinterpret_cast<const std::uint8_t*>(&len) + 4);
    out.insert(out.end(), resp.encrypted_session_key.begin(),
               resp.encrypted_session_key.end());
    return out;
}

AuthResponse deserialize_response(std::span<const std::uint8_t> buf) {
    AuthResponse resp;
    if (buf.size() < 32 + 32 + 12 + 32 + 4) return resp;
    resp.server_random.assign(buf.begin(),        buf.begin() + 32);
    resp.salt.assign(buf.begin() + 32,             buf.begin() + 64);
    std::memcpy(resp.nonce.data(), buf.data() + 64,  12);
    std::memcpy(resp.mac.data(),   buf.data() + 76,  32);
    std::uint32_t len = 0;
    std::memcpy(&len, buf.data() + 108, 4);
    if (buf.size() < 112 + len) {
        resp.server_random.clear();
        resp.salt.clear();
        return resp;
    }
    resp.encrypted_session_key.assign(buf.begin() + 112, buf.begin() + 112 + len);
    return resp;
}

}  // namespace

/// 创建客户端 Authenticator
std::unique_ptr<PskAuthenticator>
PskAuthenticator::create_client(std::span<const std::uint8_t> psk,
                                std::string_view identity) noexcept {
    auto a = std::unique_ptr<PskAuthenticator>(new PskAuthenticator());
    a->psk_.assign(psk.begin(), psk.end());
    a->identity_ = std::string(identity);
    a->is_client_ = true;
    return a;
}

/// 创建服务端 Authenticator
std::unique_ptr<PskAuthenticator>
PskAuthenticator::create_server(std::span<const std::uint8_t> psk,
                                std::string_view /*identity*/) noexcept {
    auto a = std::unique_ptr<PskAuthenticator>(new PskAuthenticator());
    a->psk_.assign(psk.begin(), psk.end());
    a->is_client_ = false;
    return a;
}

core::Result<std::vector<std::uint8_t>> PskAuthenticator::begin_handshake() noexcept {
    if (!is_client_) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INVALID_ARG);
    }
    client_request_ = psk_handshake_client_new(identity_);
    return core::Result<std::vector<std::uint8_t>>::ok(serialize_request(client_request_));
}

core::Result<std::vector<std::uint8_t>>
PskAuthenticator::process_handshake(std::span<const std::uint8_t> peer_msg) noexcept {
    if (is_client_) {
        // 客户端：处理服务端响应
        server_response_ = deserialize_response(peer_msg);
        auto dk = psk_handshake_client_finalize(psk_, client_request_, server_response_);
        if (dk.is_err()) {
            return core::Result<std::vector<std::uint8_t>>::err(dk.error());
        }
        keys_ = std::move(dk).value();
        done_ = true;
        return core::Result<std::vector<std::uint8_t>>::ok({});  // 客户端不发后续
    }
    // 服务端：处理客户端请求
    auto req = deserialize_request(peer_msg);
    if (req.client_random.empty()) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::PROTOCOL_INVALID_MSG_TYPE);
    }
    auto resp = psk_handshake_server_respond(psk_, req);
    if (resp.is_err()) {
        return core::Result<std::vector<std::uint8_t>>::err(resp.error());
    }
    server_response_ = resp.value();
    auto dk = psk_derive_session_keys(psk_, req.salt);
    if (dk.is_err()) {
        return core::Result<std::vector<std::uint8_t>>::err(dk.error());
    }
    keys_ = std::move(dk).value();
    done_ = true;
    return core::Result<std::vector<std::uint8_t>>::ok(serialize_response(server_response_));
}

core::Result<std::vector<std::uint8_t>>
PskAuthenticator::encrypt(std::span<const std::uint8_t> plaintext) noexcept {
    if (!done_) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    Nonce n{};
    auto enc = psk_aead_encrypt(keys_.enc_key, n, plaintext, {});
    if (enc.is_err()) return core::Result<std::vector<std::uint8_t>>::err(enc.error());
    // prefix nonce + ciphertext+tag（解密方需要 nonce）
    std::vector<std::uint8_t> out;
    out.reserve(12 + enc.value().size());
    out.insert(out.end(), n.begin(), n.end());
    out.insert(out.end(), enc.value().begin(), enc.value().end());
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

core::Result<std::vector<std::uint8_t>>
PskAuthenticator::decrypt(std::span<const std::uint8_t> ciphertext) noexcept {
    if (!done_) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    if (ciphertext.size() < 12) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::INVALID_ARG);
    }
    Nonce n{};
    std::memcpy(n.data(), ciphertext.data(), 12);
    return psk_aead_decrypt(keys_.enc_key, n, ciphertext.subspan(12), {});
}

core::Result<DerivedKeys> PskAuthenticator::session_keys() noexcept {
    if (!done_) {
        return core::Result<DerivedKeys>::err(core::ErrorCode::NOT_IMPLEMENTED);
    }
    return core::Result<DerivedKeys>::ok(keys_);
}

}  // namespace udaf::crypto