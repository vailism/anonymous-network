#pragma once

#include "common/crypto.hpp"
#include "common/logger.hpp"
#include "common/protocol.hpp"

#include <atomic>
#include <random>
#include <string>

namespace anet {

class RelayNode {
public:
    RelayNode(std::string name, Endpoint listenEndpoint, std::string key, int minDelayMs, int maxDelayMs);

    void run();

private:
    void handleConnection(int clientFd);

    std::string name_;
    Endpoint listenEndpoint_;
    std::string key_;
    int minDelayMs_;
    int maxDelayMs_;
    Logger logger_;
    XorCipher cipher_;
    std::mt19937 randomEngine_;
};

}  // namespace anet
