#pragma once

#include "common/protocol.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace anet {

class ScopedSocket {
public:
    explicit ScopedSocket(int fd = -1) noexcept;
    ~ScopedSocket();

    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;

    ScopedSocket(ScopedSocket&& other) noexcept;
    ScopedSocket& operator=(ScopedSocket&& other) noexcept;

    int get() const noexcept;
    bool valid() const noexcept;
    int release() noexcept;
    void reset(int fd = -1) noexcept;

private:
    int fd_;
};

int createServerSocket(std::uint16_t port, int backlog = 64);
int connectToEndpoint(const Endpoint& endpoint);
int connectWithRetries(
    const Endpoint& endpoint,
    int attempts,
    std::chrono::milliseconds delayBetweenAttempts);

bool sendFrame(int fd, const ByteBuffer& payload);
bool recvFrame(int fd, ByteBuffer& payload);

std::string peerAddress(int fd);

}  // namespace anet
