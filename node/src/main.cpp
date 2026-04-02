#include "node/relay_node.hpp"

#include "common/protocol.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr
            << "Usage: anon_node <name> <listen_port> <key> [min_delay_ms] [max_delay_ms]\n"
            << "Example: anon_node A 9001 keyA 10 90\n";
        return 1;
    }

    try {
        const std::string name = argv[1];

        const unsigned long portValue = std::stoul(argv[2]);
        if (portValue == 0 || portValue > 65535) {
            throw std::runtime_error("listen_port out of range");
        }

        const std::string key = argv[3];
        int minDelayMs = 0;
        int maxDelayMs = 0;
        if (argc >= 5) {
            minDelayMs = std::stoi(argv[4]);
        }
        if (argc == 6) {
            maxDelayMs = std::stoi(argv[5]);
        } else {
            maxDelayMs = minDelayMs;
        }

        anet::RelayNode node(
            name,
            anet::Endpoint{"127.0.0.1", static_cast<std::uint16_t>(portValue)},
            key,
            minDelayMs,
            maxDelayMs);
        node.run();
    } catch (const std::exception& ex) {
        std::cerr << "anon_node startup failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
