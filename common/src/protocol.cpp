#include "common/protocol.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace anet {
namespace {

void appendBytes(ByteBuffer& output, const void* src, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(src);
    output.insert(output.end(), bytes, bytes + length);
}

bool readBytes(
    const ByteBuffer& input,
    std::size_t& offset,
    void* destination,
    std::size_t length) {
    if (offset + length > input.size()) {
        return false;
    }

    std::memcpy(destination, input.data() + offset, length);
    offset += length;
    return true;
}

}  // namespace

std::string Endpoint::toString() const {
    return host + ":" + std::to_string(port);
}

std::optional<Endpoint> parseEndpoint(const std::string& text) {
    const auto separator = text.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator == text.size() - 1) {
        return std::nullopt;
    }

    Endpoint endpoint;
    endpoint.host = text.substr(0, separator);

    try {
        const unsigned long value = std::stoul(text.substr(separator + 1));
        if (value == 0 || value > 65535) {
            return std::nullopt;
        }
        endpoint.port = static_cast<std::uint16_t>(value);
    } catch (...) {
        return std::nullopt;
    }

    return endpoint;
}

ByteBuffer serializeInstruction(const RelayInstruction& instruction) {
    const std::string endpointText = instruction.nextHop.toString();
    if (endpointText.size() > 0xFFFF) {
        return {};
    }
    if (instruction.payload.size() > 0xFFFFFFFFULL) {
        return {};
    }

    ByteBuffer output;
    // Wire format per layer: [u16 endpoint_len][endpoint][u32 payload_len][payload].
    output.reserve(sizeof(std::uint16_t) + endpointText.size() + sizeof(std::uint32_t) + instruction.payload.size());

    const auto endpointLength = static_cast<std::uint16_t>(endpointText.size());
    const auto endpointLengthNetwork = htons(endpointLength);
    appendBytes(output, &endpointLengthNetwork, sizeof(endpointLengthNetwork));
    appendBytes(output, endpointText.data(), endpointText.size());

    const auto payloadLength = static_cast<std::uint32_t>(instruction.payload.size());
    const auto payloadLengthNetwork = htonl(payloadLength);
    appendBytes(output, &payloadLengthNetwork, sizeof(payloadLengthNetwork));
    appendBytes(output, instruction.payload.data(), instruction.payload.size());

    return output;
}

std::optional<RelayInstruction> deserializeInstruction(const ByteBuffer& bytes) {
    std::size_t offset = 0;
    std::uint16_t endpointLengthNetwork = 0;
    if (!readBytes(bytes, offset, &endpointLengthNetwork, sizeof(endpointLengthNetwork))) {
        return std::nullopt;
    }

    const std::uint16_t endpointLength = ntohs(endpointLengthNetwork);
    if (bytes.size() < sizeof(endpointLengthNetwork) + endpointLength + sizeof(std::uint32_t)) {
        return std::nullopt;
    }

    std::string endpointText(endpointLength, '\0');
    if (endpointLength > 0 && !readBytes(bytes, offset, endpointText.data(), endpointLength)) {
        return std::nullopt;
    }

    auto endpoint = parseEndpoint(endpointText);
    if (!endpoint.has_value()) {
        return std::nullopt;
    }

    std::uint32_t payloadLengthNetwork = 0;
    if (!readBytes(bytes, offset, &payloadLengthNetwork, sizeof(payloadLengthNetwork))) {
        return std::nullopt;
    }

    const std::uint32_t payloadLength = ntohl(payloadLengthNetwork);
    if (bytes.size() - offset != payloadLength) {
        return std::nullopt;
    }

    RelayInstruction instruction;
    instruction.nextHop = *endpoint;
    instruction.payload.resize(payloadLength);

    if (payloadLength > 0 && !readBytes(bytes, offset, instruction.payload.data(), payloadLength)) {
        return std::nullopt;
    }

    return instruction;
}

ByteBuffer stringToBytes(const std::string& text) {
    return ByteBuffer(text.begin(), text.end());
}

std::string bytesToString(const ByteBuffer& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::string toHexPreview(const ByteBuffer& bytes, std::size_t maxBytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    const std::size_t limit = std::min(maxBytes, bytes.size());
    for (std::size_t index = 0; index < limit; ++index) {
        stream << std::setw(2) << static_cast<int>(bytes[index]);
        if (index + 1 < limit) {
            stream << ' ';
        }
    }

    if (bytes.size() > limit) {
        stream << " ...";
    }
    stream << " (" << bytes.size() << " bytes)";

    return stream.str();
}

}  // namespace anet
