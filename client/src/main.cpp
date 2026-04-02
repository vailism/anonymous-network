#include "client/client_app.hpp"

#include "common/config.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: anon_client <config_path> <message> [hop_count]\n"
                  << "Example: anon_client config/network.conf HELLO 3\n";
        return 1;
    }

    try {
        const std::string configPath = argv[1];
        const std::string message = argv[2];

        std::size_t hopCount = 3;
        if (argc == 4) {
            hopCount = static_cast<std::size_t>(std::stoul(argv[3]));
        }

        const anet::NetworkConfig config = anet::loadNetworkConfig(configPath);
        anet::ClientApp client(config);
        client.sendAnonymousMessage(message, hopCount);
    } catch (const std::exception& ex) {
        std::cerr << "anon_client failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
