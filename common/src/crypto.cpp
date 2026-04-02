#include "common/crypto.hpp"

#include <stdexcept>

namespace anet {
namespace {

ByteBuffer xorTransform(const ByteBuffer& input, const std::string& key) {
    if (key.empty()) {
        throw std::invalid_argument("encryption key cannot be empty");
    }

    ByteBuffer output(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        output[index] = static_cast<std::uint8_t>(input[index] ^ key[index % key.size()]);
    }
    return output;
}

}  // namespace

ByteBuffer XorCipher::encrypt(const ByteBuffer& input, const std::string& key) const {
    return xorTransform(input, key);
}

ByteBuffer XorCipher::decrypt(const ByteBuffer& input, const std::string& key) const {
    return xorTransform(input, key);
}

}  // namespace anet
