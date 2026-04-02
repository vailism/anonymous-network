#include "server/final_server.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: anon_server <listen_port>\n"
                  << "Example: anon_server 9100\n";
        return 1;
    }

    try {
        const unsigned long portValue = std::stoul(argv[1]);
        if (portValue == 0 || portValue > 65535) {
            throw std::runtime_error("listen_port out of range");
        }

        anet::FinalServer server(
            anet::Endpoint{"127.0.0.1", static_cast<std::uint16_t>(portValue)});
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "anon_server startup failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
