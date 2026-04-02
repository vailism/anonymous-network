#include "client/client_app.hpp"

#include "common/socket_utils.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace anet {

ClientApp::ClientApp(NetworkConfig config)
    : config_(std::move(config))
    , logger_("CLIENT")
    , randomEngine_(std::random_device{}()) {}

void ClientApp::sendAnonymousMessage(const std::string& message, std::size_t hopCount) {
    if (hopCount < 3) {
        throw std::runtime_error("hopCount must be at least 3");
    }
    if (hopCount > config_.relays.size()) {
        throw std::runtime_error("hopCount exceeds number of available relays");
    }

    const std::vector<RelayDescriptor> route = chooseRoute(hopCount);
    logger_.info("Selected route:");
    for (std::size_t i = 0; i < route.size(); ++i) {
        logger_.info("  hop " + std::to_string(i + 1) + ": " + route[i].endpoint.toString());
    }
    logger_.info("  final: " + config_.serverEndpoint.toString());

    const ByteBuffer onionPayload = buildOnionPayload(route, stringToBytes(message));
    logger_.info("Outermost onion payload preview: " + toHexPreview(onionPayload));

    const ScopedSocket firstHop(
        connectWithRetries(route.front().endpoint, 5, std::chrono::milliseconds(150)));
    if (!sendFrame(firstHop.get(), onionPayload)) {
        throw std::runtime_error("failed to send onion payload to first hop");
    }

    logger_.info("Anonymous message dispatched to first hop " + route.front().endpoint.toString());
}

std::vector<RelayDescriptor> ClientApp::chooseRoute(std::size_t hopCount) {
    std::vector<RelayDescriptor> shuffled = config_.relays;
    std::shuffle(shuffled.begin(), shuffled.end(), randomEngine_);
    shuffled.resize(hopCount);
    return shuffled;
}

ByteBuffer ClientApp::buildOnionPayload(const std::vector<RelayDescriptor>& route, const ByteBuffer& plaintext) {
    if (route.empty()) {
        throw std::runtime_error("route cannot be empty");
    }

    // Start from final destination and wrap instructions outward hop-by-hop.
    ByteBuffer currentPayload = plaintext;
    Endpoint nextHop = config_.serverEndpoint;

    for (auto it = route.rbegin(); it != route.rend(); ++it) {
        const RelayInstruction layer{nextHop, currentPayload};
        const ByteBuffer serializedLayer = serializeInstruction(layer);
        if (serializedLayer.empty()) {
            throw std::runtime_error("failed to serialize onion layer");
        }

        logger_.info(
            "Wrapping layer for " + it->endpoint.toString() + " -> next=" + nextHop.toString() +
            ", plaintext layer=" + toHexPreview(serializedLayer));

        currentPayload = cipher_.encrypt(serializedLayer, it->key);
        logger_.info(
            "Encrypted layer for " + it->endpoint.toString() + " => " + toHexPreview(currentPayload));

        nextHop = it->endpoint;
    }

    return currentPayload;
}

}  // namespace anet
