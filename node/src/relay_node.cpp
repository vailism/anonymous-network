#include "node/relay_node.hpp"

#include "common/socket_utils.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace anet {

RelayNode::RelayNode(
    std::string name,
    Endpoint listenEndpoint,
    std::string key,
    int minDelayMs,
    int maxDelayMs)
    : name_(std::move(name))
    , listenEndpoint_(std::move(listenEndpoint))
    , key_(std::move(key))
    , minDelayMs_(std::max(0, minDelayMs))
    , maxDelayMs_(std::max(0, maxDelayMs))
    , logger_("NODE-" + name_)
    , randomEngine_(std::random_device{}()) {
    if (maxDelayMs_ < minDelayMs_) {
        maxDelayMs_ = minDelayMs_;
    }
}

void RelayNode::run() {
    const ScopedSocket listener(createServerSocket(listenEndpoint_.port));
    logger_.info("Listening on " + listenEndpoint_.toString() + " with key='" + key_ + "'");

    while (true) {
        sockaddr_storage addr{};
        socklen_t addrLen = sizeof(addr);
        const int clientFd = accept(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addrLen);
        if (clientFd < 0) {
            logger_.warn("accept() failed; continuing");
            continue;
        }

        std::thread([this, clientFd]() {
            handleConnection(clientFd);
            close(clientFd);
        }).detach();
    }
}

void RelayNode::handleConnection(int clientFd) {
    try {
        logger_.info("Incoming connection from " + peerAddress(clientFd));

        ByteBuffer encryptedFrame;
        if (!recvFrame(clientFd, encryptedFrame)) {
            logger_.warn("failed to receive framed message");
            return;
        }

        logger_.info("Incoming encrypted frame: " + toHexPreview(encryptedFrame));

        const ByteBuffer decryptedLayer = cipher_.decrypt(encryptedFrame, key_);
        logger_.info("After peeling this node's layer: " + toHexPreview(decryptedLayer));

        // A relay only learns the next hop and remaining encrypted onion payload.
        const auto instruction = deserializeInstruction(decryptedLayer);
        if (!instruction.has_value()) {
            logger_.warn("invalid relay instruction after decrypt; dropping packet");
            return;
        }

        logger_.info("Forward destination discovered: " + instruction->nextHop.toString());

        if (maxDelayMs_ > 0) {
            std::uniform_int_distribution<int> delayDist(minDelayMs_, maxDelayMs_);
            const int delay = delayDist(randomEngine_);
            if (delay > 0) {
                logger_.info("Applying random delay of " + std::to_string(delay) + " ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }

        const ScopedSocket nextHopSocket(
            connectWithRetries(instruction->nextHop, 3, std::chrono::milliseconds(120)));

        if (!sendFrame(nextHopSocket.get(), instruction->payload)) {
            logger_.warn("failed to forward payload to " + instruction->nextHop.toString());
            return;
        }

        logger_.info(
            "Forwarded remaining onion payload to " + instruction->nextHop.toString() +
            " => " + toHexPreview(instruction->payload));
    } catch (const std::exception& ex) {
        logger_.error(std::string("connection handler failed: ") + ex.what());
    }
}

}  // namespace anet
