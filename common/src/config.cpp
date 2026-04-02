#include "common/config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace anet {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::unordered_map<std::string, std::string> parseKeyValueFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open config file: " + path);
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        const auto cleaned = trim(line);
        if (cleaned.empty() || cleaned[0] == '#') {
            continue;
        }

        const auto separator = cleaned.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("invalid config line " + std::to_string(lineNumber));
        }

        const auto key = trim(cleaned.substr(0, separator));
        const auto value = trim(cleaned.substr(separator + 1));
        values[key] = value;
    }

    return values;
}

}  // namespace

NetworkConfig loadNetworkConfig(const std::string& path) {
    const auto values = parseKeyValueFile(path);

    NetworkConfig config;

    const auto serverIt = values.find("server");
    if (serverIt == values.end()) {
        throw std::runtime_error("missing 'server' in config");
    }
    const auto serverEndpoint = parseEndpoint(serverIt->second);
    if (!serverEndpoint.has_value()) {
        throw std::runtime_error("invalid server endpoint: " + serverIt->second);
    }
    config.serverEndpoint = *serverEndpoint;

    for (std::size_t index = 1;; ++index) {
        const std::string endpointKey = "relay" + std::to_string(index) + ".endpoint";
        const std::string keyKey = "relay" + std::to_string(index) + ".key";

        const auto endpointIt = values.find(endpointKey);
        const auto keyIt = values.find(keyKey);

        if (endpointIt == values.end() && keyIt == values.end()) {
            break;
        }
        if (endpointIt == values.end() || keyIt == values.end()) {
            throw std::runtime_error("incomplete relay entry for index " + std::to_string(index));
        }

        auto endpoint = parseEndpoint(endpointIt->second);
        if (!endpoint.has_value()) {
            throw std::runtime_error("invalid relay endpoint: " + endpointIt->second);
        }
        if (keyIt->second.empty()) {
            throw std::runtime_error("relay key cannot be empty for index " + std::to_string(index));
        }

        config.relays.push_back(RelayDescriptor{*endpoint, keyIt->second});
    }

    if (config.relays.size() < 3) {
        throw std::runtime_error("config must define at least 3 relays");
    }

    return config;
}

}  // namespace anet
