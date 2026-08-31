// udp_socket.cpp - UdpSocket 实现（UDP + 频率限制 + 可选 PSK）
#include "udp_socket.hpp"

#include "core/log/logger.hpp"
#include "crypto/psk.hpp"
#include "platform/fs/unique_fd.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace udaf::ability_a::transport {

namespace {

core::Result<void> set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return core::Result<void>::err(core::ErrorCode::NET_SEND_FAILED);
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return core::Result<void>::err(core::ErrorCode::NET_SEND_FAILED);
    }
    return core::Result<void>::ok();
}

}  // namespace

core::Result<std::unique_ptr<UdpSocket>>
UdpSocket::create(std::uint16_t bind_port) noexcept {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return core::Result<std::unique_ptr<UdpSocket>>::err(core::ErrorCode::NET_SEND_FAILED);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(bind_port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        ::close(fd);
        if (err == EADDRINUSE) {
            return core::Result<std::unique_ptr<UdpSocket>>::err(core::ErrorCode::RESOURCE_BUSY);
        }
        return core::Result<std::unique_ptr<UdpSocket>>::err(core::ErrorCode::NET_SEND_FAILED);
    }

    // 查询实际绑定端口
    sockaddr_in actual{};
    socklen_t len = sizeof(actual);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &len) < 0) {
        ::close(fd);
        return core::Result<std::unique_ptr<UdpSocket>>::err(core::ErrorCode::NET_SEND_FAILED);
    }

    auto sock = std::unique_ptr<UdpSocket>(new UdpSocket());
    sock->fd_ = fd;
    sock->bound_port_ = ntohs(actual.sin_port);
    auto nb = set_nonblocking(fd);
    if (nb.is_err()) {
        ::close(fd);
        return core::Result<std::unique_ptr<UdpSocket>>::err(nb.error());
    }
    return core::Result<std::unique_ptr<UdpSocket>>::ok(std::move(sock));
}

UdpSocket::~UdpSocket() { close(); }

void UdpSocket::close() noexcept {
    if (fd_ >= 0) {
        int ret = 0;
        do { ret = ::close(fd_); } while (ret < 0 && errno == EINTR);
        fd_ = -1;
    }
}

std::uint16_t UdpSocket::bound_port() const noexcept { return bound_port_; }

core::Result<void> UdpSocket::enable_broadcast() const noexcept {
    int on = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST,
                     &on, sizeof(on)) < 0) {
        return core::Result<void>::err(core::ErrorCode::NET_BROADCAST_FAILED);
    }
    return core::Result<void>::ok();
}

void UdpSocket::set_psk(std::span<const std::uint8_t> psk) noexcept {
    psk_.assign(psk.begin(), psk.end());
}

bool UdpSocket::check_unicast_rate(const std::string& addr) noexcept {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(unicast_mtx_);
    auto& hist = unicast_history_[addr];
    // 移除 1s 之外的时间戳
    while (!hist.empty() && now - hist.front() > std::chrono::seconds(1)) {
        hist.erase(hist.begin());
    }
    if (static_cast<int>(hist.size()) >= kUnicastMaxPerSec) return false;
    hist.push_back(now);
    return true;
}

bool UdpSocket::check_broadcast_rate() noexcept {
    auto now = std::chrono::steady_clock::now();
    if (last_broadcast_at_.time_since_epoch().count() != 0 &&
        now - last_broadcast_at_ < kBroadcastPeriod) {
        return false;
    }
    last_broadcast_at_ = now;
    return true;
}

core::Result<std::size_t>
UdpSocket::send(std::span<const std::uint8_t> payload, const Endpoint& dst) noexcept {
    if (fd_ < 0) {
        return core::Result<std::size_t>::err(core::ErrorCode::NET_SOCKET_CLOSED);
    }
    if (dst.is_broadcast) {
        if (!check_broadcast_rate()) {
            return core::Result<std::size_t>::err(core::ErrorCode::NET_RATE_LIMITED);
        }
    } else {
        if (!check_unicast_rate(dst.address)) {
            return core::Result<std::size_t>::err(core::ErrorCode::NET_RATE_LIMITED);
        }
    }

    // PSK 加密（可选）：nonce prefix + ciphertext+tag
    std::vector<std::uint8_t> to_send;
    if (psk_.size() == 32 && !payload.empty()) {
        udaf::crypto::Nonce n{};
        auto enc = udaf::crypto::psk_aead_encrypt(psk_, n, payload, {});
        if (enc.is_err()) {
            return core::Result<std::size_t>::err(enc.error());
        }
        to_send.reserve(12 + enc.value().size());
        to_send.insert(to_send.end(), n.begin(), n.end());
        to_send.insert(to_send.end(), enc.value().begin(), enc.value().end());
    } else {
        to_send.assign(payload.begin(), payload.end());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dst.port);
    if (::inet_pton(AF_INET, dst.address.c_str(), &addr.sin_addr) != 1) {
        return core::Result<std::size_t>::err(core::ErrorCode::INVALID_ARG);
    }

    ssize_t n = ::sendto(fd_, to_send.data(), to_send.size(), 0,
                         reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (n < 0) {
        if (errno == EAGAIN) {
            return core::Result<std::size_t>::err(core::ErrorCode::NET_TIMEOUT);
        }
        return core::Result<std::size_t>::err(core::ErrorCode::NET_SEND_FAILED);
    }
    return core::Result<std::size_t>::ok(static_cast<std::size_t>(n));
}

core::Result<std::vector<std::uint8_t>>
UdpSocket::recv(int timeout_ms) noexcept {
    if (fd_ < 0) {
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NET_SOCKET_CLOSED);
    }
    // 简易 select 超时
    if (timeout_ms >= 0) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd_, &rd);
        timeval tv{
            static_cast<__time_t>(timeout_ms / 1000),
            static_cast<__suseconds_t>((timeout_ms % 1000) * 1000)
        };
        int ret = ::select(fd_ + 1, &rd, nullptr, nullptr, &tv);
        if (ret == 0) return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NET_TIMEOUT);
        if (ret < 0)  return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NET_SEND_FAILED);
    }

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(64) * 1024);
    sockaddr_in src{};
    socklen_t srclen = sizeof(src);
    ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                            reinterpret_cast<sockaddr*>(&src), &srclen);
    if (n < 0) {
        if (errno == EAGAIN) {
            return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NET_TIMEOUT);
        }
        return core::Result<std::vector<std::uint8_t>>::err(core::ErrorCode::NET_SEND_FAILED);
    }
    buf.resize(static_cast<std::size_t>(n));

    // PSK 解密（若启用）
    if (psk_.size() == 32 && buf.size() > 12) {
        udaf::crypto::Nonce n2{};
        std::memcpy(n2.data(), buf.data(), 12);
        auto dec = udaf::crypto::psk_aead_decrypt(psk_, n2,
                                                   std::span<const std::uint8_t>(
                                                       buf.data() + 12, buf.size() - 12),
                                                   {});
        if (dec.is_err()) {
            return core::Result<std::vector<std::uint8_t>>::err(dec.error());
        }
        return core::Result<std::vector<std::uint8_t>>::ok(std::move(dec).value());
    }
    return core::Result<std::vector<std::uint8_t>>::ok(std::move(buf));
}

}  // namespace udaf::ability_a::transport