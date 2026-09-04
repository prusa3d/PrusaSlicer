#include "Slic3r/Biz/SHA256.hpp"

#include <openssl/evp.h>

namespace Slic3r::Biz {

std::string sha256(const std::string& input)
{
    EVP_MD_CTX* mdctx;
    const EVP_MD* md;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen;

    md = EVP_sha256();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, input.c_str(), input.length());
    EVP_DigestFinal_ex(mdctx, digest, &digestLen);
    EVP_MD_CTX_free(mdctx);

    return std::string(reinterpret_cast<char*>(digest), digestLen);
}
}