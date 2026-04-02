#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace anet {

using ByteBuffer = std::vector<std::uint8_t>;

struct Endpoint {
    std::string host;
    std::uint16_t port{0};

    std::string toString() const;
};

std::optional<Endpoint> parseEndpoint(const std::string& text);

struct RelayInstruction {
    Endpoint nextHop;
    ByteBuffer payload;
};

ByteBuffer serializeInstruction(const RelayInstruction& instruction);
std::optional<RelayInstruction> deserializeInstruction(const ByteBuffer& bytes);

ByteBuffer stringToBytes(const std::string& text);
std::string bytesToString(const ByteBuffer& bytes);
std::string toHexPreview(const ByteBuffer& bytes, std::size_t maxBytes = 24);

}  // namespace anet
