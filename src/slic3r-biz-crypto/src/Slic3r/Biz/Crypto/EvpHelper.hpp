#pragma once

#include <concepts>
#include <openssl/evp.h>

#include "Slic3r/Biz/Crypto/Hash.hpp"

namespace Slic3r::Biz::Crypto {

template <typename T>
concept HashBuilderDesc = requires
{
    requires std::same_as<decltype(T::get()), const EVP_MD*>;
    requires std::same_as<decltype(T::type()), HashType>;
};

#define BUILDER_DESC_STRUCT(name, evp_getter, hash_type)   \
struct name                                             \
{                                                       \
    static auto get() { return evp_getter(); }     \
    static auto type() { return HashType::hash_type; }  \
    static size_t bytes_size() { return EVP_MD_size(evp_getter()); }  \
};

BUILDER_DESC_STRUCT(Sha1, EVP_sha1, SHA_1);
BUILDER_DESC_STRUCT(Sha256, EVP_sha256, SHA_256);
BUILDER_DESC_STRUCT(Sha512, EVP_sha512, SHA_512);

#undef BUILDER_DESC_STRUCT

class EvpContextOwner
{
protected:
    EvpContextOwner(): m_context(EVP_MD_CTX_new(), ::EVP_MD_CTX_free) {}
    virtual ~EvpContextOwner() = default;

protected:
    using ContextPtr = std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;
    ContextPtr m_context;
};



} // namespace Slic3r::Biz::Crypto

