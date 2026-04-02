#pragma once

#include "common/config.hpp"
#include "common/crypto.hpp"
#include "common/logger.hpp"

#include <random>
#include <string>
#include <vector>

namespace anet {

class ClientApp {
public:
    explicit ClientApp(NetworkConfig config);

    void sendAnonymousMessage(const std::string& message, std::size_t hopCount);

private:
    std::vector<RelayDescriptor> chooseRoute(std::size_t hopCount);
    ByteBuffer buildOnionPayload(const std::vector<RelayDescriptor>& route, const ByteBuffer& plaintext);

    NetworkConfig config_;
    Logger logger_;
    XorCipher cipher_;
    std::mt19937 randomEngine_;
};

}  // namespace anet
