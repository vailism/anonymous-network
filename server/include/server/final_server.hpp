#pragma once

#include "common/logger.hpp"
#include "common/protocol.hpp"

namespace anet {

class FinalServer {
public:
    explicit FinalServer(Endpoint listenEndpoint);

    void run();

private:
    void handleConnection(int clientFd);

    Endpoint listenEndpoint_;
    Logger logger_;
};

}  // namespace anet
