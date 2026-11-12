#pragma once

#include <string_view>
#include <memory>

#include "Slic3r/Biz/Crypto/Types.hpp"

namespace  Slic3r::Biz::Crypto {

enum class HashType
{
    SHA_1,
    SHA_256,
    SHA_512
};

class Hash
{
public:
    Hash(HashType type, Bytes bytes)
        : m_type(type), m_bytes(std::move(bytes)) {}

    Hash(const Hash&) = default;
    Hash(Hash&&) = default;
    Hash& operator=(const Hash&) = default;
    Hash& operator=(Hash&&) = default;

    bool operator==(const Hash& other) const
    {
        return m_type == other.m_type && m_bytes == other.m_bytes;
    }

    bool operator!=(const Hash& other) const
    {
        return !(*this == other);
    }

    HashType type() const { return m_type; }
    const Bytes& bytes() const { return m_bytes; }

    std::string hex_string(std::string_view sep="") const;

    static size_t byte_size(HashType type);

private:
    HashType m_type{HashType::SHA_256};
    Bytes m_bytes;
};

class IHashBuilder
{
public:
    virtual ~IHashBuilder() = default;

    virtual void update(BytesView bytes_view) = 0;
    virtual Hash finalize()                   = 0;
};

std::unique_ptr<IHashBuilder> create_hash_builder(HashType type);

Hash compute_hash(BytesView bytes_view, HashType type = HashType::SHA_256);
Hash compute_file_hash(const std::string& file_path, HashType type = HashType::SHA_256);


} // namespace Slic3r::Biz::Algorithms::Crypto
