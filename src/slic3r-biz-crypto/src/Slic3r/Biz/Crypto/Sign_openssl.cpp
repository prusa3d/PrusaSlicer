#include "Slic3r/Biz/Crypto/Sign.hpp"
#include "Slic3r/Biz/Crypto/EvpHelper.hpp"

#include <fmt/format.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

namespace Slic3r::Biz::Crypto {

namespace Internal {

struct KeyImpl : std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> {
    using Base = std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)>;

    KeyImpl() : Base(EVP_PKEY_new(), ::EVP_PKEY_free) {}
    explicit KeyImpl(EVP_PKEY* key) : Base(key, ::EVP_PKEY_free) {}

    KeyImpl(KeyImpl&&) = default;

    KeyImpl& operator=(KeyImpl&&) = default;
};

} // namespace Internal

namespace {
[[noreturn]] void throw_ssl_error(std::string_view what) {
    char err_buf[256]{};
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof err_buf);
    throw CryptoException(std::string{what} + ": " + err_buf);
}

template <typename T = SecureString>
T bio_to_string(BIO* bio) {
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    if (!mem || !mem->data) {
        throw_ssl_error("BIO_get_mem_ptr");
    }
    return {mem->data, mem->length};
}
}

using BioPtr = std::unique_ptr<BIO, decltype(&::BIO_free)>;

BioPtr make_bio_ptr(BIO* bio)
{
    return BioPtr{bio, &::BIO_free};
}

KeyPair::KeyPair() : m_key(std::make_unique<Internal::KeyImpl>()) {}
KeyPair::~KeyPair() = default;

KeyPair::KeyPair(KeyPair&&) noexcept = default;

KeyPair& KeyPair::operator=(KeyPair&&) noexcept = default;

KeyPair::KeyPair(ImplPtr&& impl) : m_key(std::move(impl)) {}

SecureString KeyPair::save_private_key_pem() const
{
    BioPtr bio = make_bio_ptr(BIO_new(BIO_s_mem()));
    if (!bio)
        throw_ssl_error("BIO_new");

    std::string_view passphrase;
    const EVP_CIPHER* cipher = passphrase.empty() ? nullptr : EVP_aes_256_cbc();
    const auto* key_str =
        passphrase.empty() ? nullptr : reinterpret_cast<const unsigned char*>(passphrase.data());

    if (PEM_write_bio_PrivateKey(
            bio.get(),
            internal_impl().get(),
            cipher,
            key_str,
            static_cast<int>(passphrase.size()),
            nullptr,
            nullptr
        )
        != 1)
    {
        throw_ssl_error("PEM_write_bio_PrivateKey");
    }
    return bio_to_string(bio.get());
}

std::string KeyPair::save_public_key_pem() const
{
    BioPtr bio = make_bio_ptr(BIO_new(BIO_s_mem()));
    if (!bio) {
        throw_ssl_error("BIO_new");
    }

    if (PEM_write_bio_PUBKEY(bio.get(), internal_impl().get()) != 1) {
        throw_ssl_error("PEM_write_bio_PUBKEY");
    }

    return bio_to_string<std::string>(bio.get());
}

KeyPair KeyPair::generate(const char* algo, int size)
{
    Internal::KeyImpl impl{
        EVP_PKEY_Q_keygen(nullptr, nullptr, algo, size)
    };
    if (impl == nullptr) {
        throw CryptoException(fmt::format("Unsupported keygen cypher {} or its size {}", algo, size));
    }
    auto ptr = std::make_unique<Internal::KeyImpl>(std::move(impl));
    return KeyPair{std::move(ptr)};
}

KeyPair KeyPair::generate(int size)
{
    return generate("RSA", size);
}

KeyPair KeyPair::load_pub_pem(BytesView bytes)
{
    auto bio = make_bio_ptr(BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size())));
    Internal::KeyImpl impl = Internal::KeyImpl(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
    if (impl == nullptr) {
        throw CryptoException("Reading PEM failed");
    }

    ImplPtr ptr{std::make_unique<Internal::KeyImpl>(std::move(impl))};
    return KeyPair{std::move(ptr)};
}

KeyPair KeyPair::load_priv_pem(BytesView bytes)
{
    auto bio = make_bio_ptr(BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size())));
    Internal::KeyImpl impl = Internal::KeyImpl(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    if (impl == nullptr) {
        throw CryptoException("Reading PEM failed");
    }

    ImplPtr ptr{std::make_unique<Internal::KeyImpl>(std::move(impl))};
    return KeyPair{std::move(ptr)};
}

template <HashBuilderDesc DescT>
class SignatureBuilder : public ISignatureBuilder, EvpContextOwner
{
public:
    explicit SignatureBuilder(const KeyPair& key)
    {
        EVP_DigestSignInit(
            m_context.get(),
            nullptr,
            DescT::get(),
            nullptr,
            key.internal_impl().get()
        );
    }

    void update(BytesView bytes) override
    {
        EVP_DigestSignUpdate(m_context.get(), bytes.data(), static_cast<int>(bytes.size()));
    }

    Signature finalize() override
    {
        size_t sign_len{0};
        EVP_DigestSignFinal(m_context.get(), nullptr, &sign_len);
        Bytes raw_sign;
        raw_sign.resize(sign_len);
        EVP_DigestSignFinal(m_context.get(), raw_sign.data(), &sign_len);
        return Signature{std::move(raw_sign)};
    }
};

ISignatureBuilderPtr create_signature_builder(HashType hash_type, const KeyPair& key_pair)
{
#define CASE_IMPL(HASH_TYPE, DESC_TYPE) \
    case HashType::HASH_TYPE: \
        return std::make_unique<SignatureBuilder<DESC_TYPE>>(key_pair);

    switch (hash_type) {
        CASE_IMPL(SHA_1, Sha1)
        CASE_IMPL(SHA_256, Sha256)
        CASE_IMPL(SHA_512, Sha512)
    }

#undef CASE_IMPL

    return nullptr;
}

template <HashBuilderDesc DescT>
class SignatureVerifier : public ISignatureVerifier, EvpContextOwner
{
public:
    explicit SignatureVerifier(const KeyPair& key)
    {
        auto result = EVP_DigestVerifyInit(m_context.get(), nullptr, DescT::get(), nullptr, key.internal_impl().get());
        m_failed = result <= 0;
    }

    void update(BytesView bytes) override
    {
        if (!m_failed) {
            auto result = EVP_DigestVerifyUpdate(m_context.get(), bytes.data(), static_cast<int>(bytes.size()));
            m_failed = result != 1;
        }
    }

    bool finalize(const Signature& signature) override
    {
        if (!m_failed) {
            auto result = EVP_DigestVerifyFinal(m_context.get(), signature.bytes().data(), signature.bytes().size());
            m_failed = result != 1;
        }
        return !m_failed;
    }
private:
    bool m_failed{false};
};

ISignatureVerifierPtr create_signature_verifier(HashType hash_type, const KeyPair& key_pair)
{
#define CASE_IMPL(HASH_TYPE, DESC_TYPE) \
case HashType::HASH_TYPE: \
return std::make_unique<SignatureVerifier<DESC_TYPE>>(key_pair);

    switch (hash_type) {
        CASE_IMPL(SHA_1, Sha1)
        CASE_IMPL(SHA_256, Sha256)
        CASE_IMPL(SHA_512, Sha512)
    }

#undef CASE_IMPL

    return nullptr;

}
} // namespace Slic3r::Biz::Crypto
