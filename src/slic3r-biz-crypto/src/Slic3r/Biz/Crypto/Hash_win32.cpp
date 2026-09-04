#include "Slic3r/Biz/Crypto/Hash.hpp"
#include "Slic3r/Assert.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

namespace Slic3r::Biz::Crypto {

class CngHashBuilder : public IHashBuilder
{
public:
    CngHashBuilder(LPCWSTR pszAlgId, HashType type, size_t expected_size)
        : m_type(type), m_expected_size(expected_size)
    {
        NTSTATUS status = BCryptOpenAlgorithmProvider(&m_hAlg, pszAlgId, nullptr, 0);
        ASSERT(BCRYPT_SUCCESS(status));

        status = BCryptCreateHash(m_hAlg, &m_hHash, nullptr, 0, nullptr, 0, 0);
        ASSERT(BCRYPT_SUCCESS(status));
    }

    ~CngHashBuilder() override
    {
        if (m_hHash) {
            BCryptDestroyHash(m_hHash);
        }
        if (m_hAlg) {
            BCryptCloseAlgorithmProvider(m_hAlg, 0);
        }
    }

    // Disable copy/move to safely manage HANDLEs
    CngHashBuilder(const CngHashBuilder&) = delete;
    CngHashBuilder& operator=(const CngHashBuilder&) = delete;

    void update(BytesView bytes_view) override
    {
        if (!bytes_view.empty()) {
            BCryptHashData(m_hHash, (PUCHAR)bytes_view.data(), (ULONG)bytes_view.size(), 0);
        }
    }

    Hash finalize() override
    {
        Bytes bytes;
        bytes.resize(m_expected_size);

        NTSTATUS status = BCryptFinishHash(m_hHash, (PUCHAR)bytes.data(), (ULONG)bytes.size(), 0);
        ASSERT(BCRYPT_SUCCESS(status));

        return {m_type, std::move(bytes)};
    }

private:
    BCRYPT_ALG_HANDLE  m_hAlg{nullptr};
    BCRYPT_HASH_HANDLE m_hHash{nullptr};
    HashType           m_type;
    size_t             m_expected_size;
};

std::unique_ptr<IHashBuilder> create_hash_builder(HashType type)
{
    switch (type) {
        case HashType::SHA_1:
            return std::make_unique<CngHashBuilder>(BCRYPT_SHA1_ALGORITHM, type, 20);
        case HashType::SHA_256:
            return std::make_unique<CngHashBuilder>(BCRYPT_SHA256_ALGORITHM, type, 32);
        case HashType::SHA_512:
            return std::make_unique<CngHashBuilder>(BCRYPT_SHA512_ALGORITHM, type, 64);
        default:
            return nullptr;
    }
}

} // namespace Slic3r::Biz::Crypto
