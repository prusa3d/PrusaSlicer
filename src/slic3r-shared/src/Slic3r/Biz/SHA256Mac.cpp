#include "Slic3r/Biz/SHA256.hpp"

#include <CommonCrypto/CommonDigest.h>

namespace Slic3r::Biz {

std::string sha256(const std::string& input)
{
    // Initialize the context
    CC_SHA256_CTX sha256;
    CC_SHA256_Init(&sha256);

    // Update the context with the input data
    CC_SHA256_Update(&sha256, input.c_str(), static_cast<CC_LONG>(input.length()));

    // Finalize the hash and retrieve the result
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &sha256);

    return std::string(reinterpret_cast<char*>(digest), CC_SHA256_DIGEST_LENGTH);
}
}