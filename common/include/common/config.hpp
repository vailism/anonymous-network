#pragma once

#include "common/protocol.hpp"

#include <string>
#include <vector>

namespace anet {

struct RelayDescriptor {
    Endpoint endpoint;
    std::string key;
};

struct NetworkConfig {
    Endpoint serverEndpoint;
    std::vector<RelayDescriptor> relays;
};

NetworkConfig loadNetworkConfig(const std::string& path);

}  // namespace anet
