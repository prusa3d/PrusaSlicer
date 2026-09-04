#include "Slic3r/Biz/Crypto/Hash.hpp"
#include "Slic3r/Biz/Crypto/EvpHelper.hpp"

#include <openssl/evp.h>

namespace Slic3r::Biz::Crypto {

template<HashBuilderDesc DescT>
class EvpHashBuilder : public IHashBuilder, EvpContextOwner
{
public:
    EvpHashBuilder() : m_md(DescT::get())
    {
        EVP_DigestInit_ex(m_context.get(), m_md, nullptr);
    }

    void update(BytesView bytes_view) override
    {
        if (!bytes_view.empty()) {
            EVP_DigestUpdate(m_context.get(), bytes_view.data(), bytes_view.size());
        }
    }

    Hash finalize() override
    {
        Bytes bytes;
        bytes.resize(EVP_MD_size(m_md));
        uint32_t len;
        EVP_DigestFinal_ex(m_context.get(), bytes.data(), &len);

        return {DescT::type(), std::move(bytes)};
    }

private:
    const EVP_MD* m_md{nullptr};
};

std::unique_ptr<IHashBuilder> create_hash_builder(HashType type)
{
#define CASE_IMPL(name, hash_type)                       \
    case HashType::hash_type:                            \
        return std::make_unique<EvpHashBuilder<name>>();

    switch (type) {
        CASE_IMPL(Sha1, SHA_1)
        CASE_IMPL(Sha256, SHA_256)
        CASE_IMPL(Sha512, SHA_512)
    }

#undef CASE_IMPL

    return nullptr;
}

} // namespace Slic3r::Biz::Crypto
