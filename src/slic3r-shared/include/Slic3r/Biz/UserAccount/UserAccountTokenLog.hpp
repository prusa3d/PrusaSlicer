#pragma once

#include "Slic3r/Biz/SHA256.hpp"

#include <string>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Builds a non-reversible identifier of a token for log files.
 *
 * No part of the token itself is ever returned. The value is stable for a given
 * token, so logs of two instances can still be matched against each other.
 */
inline std::string token_log_fingerprint(const std::string& token)
{
    if (token.empty()) {
        return "[empty]";
    }
    try {
        const std::string digest = sha256(token);
        static constexpr char hex_digits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(16);
        for (size_t i = 0; i < 8 && i < digest.size(); ++i) {
            const auto byte = static_cast<unsigned char>(digest[i]);
            hex += hex_digits[byte >> 4];
            hex += hex_digits[byte & 0x0F];
        }
        return "sha256:" + hex + " len:" + std::to_string(token.size());
    } catch (...) {
        return "[hashing failed] len:" + std::to_string(token.size());
    }
}

} // namespace Slic3r::Biz::UserAccount
