#pragma once

#include "common/protocol.hpp"

#include <string>

namespace anet {

class Cipher {
public:
    virtual ~Cipher() = default;

    virtual ByteBuffer encrypt(const ByteBuffer& input, const std::string& key) const = 0;
    virtual ByteBuffer decrypt(const ByteBuffer& input, const std::string& key) const = 0;
};

class XorCipher final : public Cipher {
public:
    ByteBuffer encrypt(const ByteBuffer& input, const std::string& key) const override;
    ByteBuffer decrypt(const ByteBuffer& input, const std::string& key) const override;
};

}  // namespace anet
