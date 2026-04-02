#include "server/final_server.hpp"

#include "common/socket_utils.hpp"

#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace anet {

FinalServer::FinalServer(Endpoint listenEndpoint)
    : listenEndpoint_(std::move(listenEndpoint))
    , logger_("FINAL-SERVER") {}

void FinalServer::run() {
    const ScopedSocket listener(createServerSocket(listenEndpoint_.port));
    logger_.info("Final receiver listening on " + listenEndpoint_.toString());

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

void FinalServer::handleConnection(int clientFd) {
    logger_.info("Incoming connection from " + peerAddress(clientFd));

    ByteBuffer plaintextPayload;
    if (!recvFrame(clientFd, plaintextPayload)) {
        logger_.warn("failed to receive final payload");
        return;
    }

    const std::string message = bytesToString(plaintextPayload);
    logger_.info("Final plaintext received: '" + message + "'");
    logger_.info("Payload bytes: " + toHexPreview(plaintextPayload));
}

}  // namespace anet
