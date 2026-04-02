#include "common/socket_utils.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace anet {
namespace {

void setReuseAddr(int fd) {
    constexpr int option = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        throw std::runtime_error(std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
    }
}

void sendAll(int fd, const std::uint8_t* data, std::size_t length) {
    std::size_t totalSent = 0;
    while (totalSent < length) {
        const ssize_t sent = send(fd, data + totalSent, length - totalSent, 0);
        if (sent <= 0) {
            throw std::runtime_error(std::string("send failed: ") + std::strerror(errno));
        }
        totalSent += static_cast<std::size_t>(sent);
    }
}

void recvAll(int fd, std::uint8_t* data, std::size_t length) {
    std::size_t totalRead = 0;
    while (totalRead < length) {
        const ssize_t received = recv(fd, data + totalRead, length - totalRead, 0);
        if (received <= 0) {
            throw std::runtime_error(std::string("recv failed: ") + std::strerror(errno));
        }
        totalRead += static_cast<std::size_t>(received);
    }
}

int createSocketWithHints(const Endpoint& endpoint, const addrinfo* hintsTemplate) {
    addrinfo* result = nullptr;
    const std::string portText = std::to_string(endpoint.port);

    const int status = getaddrinfo(endpoint.host.c_str(), portText.c_str(), hintsTemplate, &result);
    if (status != 0) {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(status));
    }

    int socketFd = -1;
    for (const addrinfo* current = result; current != nullptr; current = current->ai_next) {
        socketFd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        if (::connect(socketFd, current->ai_addr, current->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return socketFd;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);
    throw std::runtime_error(std::string("unable to connect to ") + endpoint.toString());
}

}  // namespace

ScopedSocket::ScopedSocket(int fd) noexcept
    : fd_(fd) {}

ScopedSocket::~ScopedSocket() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

ScopedSocket::ScopedSocket(ScopedSocket&& other) noexcept
    : fd_(other.fd_) {
    other.fd_ = -1;
}

ScopedSocket& ScopedSocket::operator=(ScopedSocket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int ScopedSocket::get() const noexcept {
    return fd_;
}

bool ScopedSocket::valid() const noexcept {
    return fd_ >= 0;
}

int ScopedSocket::release() noexcept {
    const int tmp = fd_;
    fd_ = -1;
    return tmp;
}

void ScopedSocket::reset(int fd) noexcept {
    if (fd_ >= 0) {
        close(fd_);
    }
    fd_ = fd;
}

int createServerSocket(std::uint16_t port, int backlog) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket() failed: ") + std::strerror(errno));
    }

    ScopedSocket guard(fd);
    setReuseAddr(fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error(std::string("bind() failed: ") + std::strerror(errno));
    }

    if (listen(fd, backlog) < 0) {
        throw std::runtime_error(std::string("listen() failed: ") + std::strerror(errno));
    }

    return guard.release();
}

int connectToEndpoint(const Endpoint& endpoint) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    return createSocketWithHints(endpoint, &hints);
}

int connectWithRetries(
    const Endpoint& endpoint,
    int attempts,
    std::chrono::milliseconds delayBetweenAttempts) {
    if (attempts <= 0) {
        attempts = 1;
    }

    std::string finalError = "connection attempts exhausted";
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        try {
            return connectToEndpoint(endpoint);
        } catch (const std::exception& ex) {
            finalError = ex.what();
            if (attempt < attempts) {
                std::this_thread::sleep_for(delayBetweenAttempts);
            }
        }
    }

    throw std::runtime_error(finalError);
}

bool sendFrame(int fd, const ByteBuffer& payload) {
    if (payload.size() > 0xFFFFFFFFULL) {
        return false;
    }

    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    const std::uint32_t networkLength = htonl(length);

    try {
        // All TCP messages use a 4-byte length prefix followed by payload bytes.
        sendAll(fd, reinterpret_cast<const std::uint8_t*>(&networkLength), sizeof(networkLength));
        if (!payload.empty()) {
            sendAll(fd, payload.data(), payload.size());
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool recvFrame(int fd, ByteBuffer& payload) {
    std::uint32_t networkLength = 0;
    try {
        // Read exactly one full framed message from the stream socket.
        recvAll(fd, reinterpret_cast<std::uint8_t*>(&networkLength), sizeof(networkLength));
        const std::uint32_t length = ntohl(networkLength);
        payload.resize(length);
        if (length > 0) {
            recvAll(fd, payload.data(), length);
        }
        return true;
    } catch (...) {
        payload.clear();
        return false;
    }
}

std::string peerAddress(int fd) {
    sockaddr_storage addr{};
    socklen_t addrLen = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
        return "unknown";
    }

    char hostBuffer[NI_MAXHOST] = {0};
    char serviceBuffer[NI_MAXSERV] = {0};
    const int status = getnameinfo(
        reinterpret_cast<const sockaddr*>(&addr),
        addrLen,
        hostBuffer,
        sizeof(hostBuffer),
        serviceBuffer,
        sizeof(serviceBuffer),
        NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        return "unknown";
    }

    return std::string(hostBuffer) + ":" + serviceBuffer;
}

}  // namespace anet
