#pragma once

#include "Slic3r/Biz/Crypto/Types.hpp"
#include "Slic3r/Biz/Crypto/Hash.hpp"
#include "Slic3r/Biz/Crypto/SecureString.hpp"

#include <memory>

namespace Slic3r::Biz::Crypto {
namespace Internal {
struct KeyImpl;
}

class KeyPair
{
public:
    KeyPair();
    ~KeyPair();

    KeyPair(KeyPair&&) noexcept ;
    KeyPair& operator=(KeyPair&&) noexcept;

    KeyPair(const KeyPair&) = delete;
    KeyPair& operator=(const KeyPair&) = delete;

    SecureString save_private_key_pem() const;
    std::string save_public_key_pem() const;

    static KeyPair generate(const char* algo, int size);
    static KeyPair generate(int size=2048);
    static KeyPair load_pub_pem(BytesView bytes);
    static KeyPair load_priv_pem(BytesView bytes);

    const Internal::KeyImpl& internal_impl() const { return *m_key; }
    Internal::KeyImpl& internal_impl() { return *m_key; }
private:
    using ImplPtr = std::unique_ptr<Internal::KeyImpl>;
    explicit KeyPair(ImplPtr&& impl);
private:
    ImplPtr m_key;
};

class Signature
{
public:
    Signature() = default;
    Signature(Signature&&) = default;
    Signature(const Signature&) = delete;

    Signature& operator=(Signature&&) = default;
    Signature& operator=(const Signature&) = default;

    explicit Signature(Bytes bytes) : m_bytes(std::move(bytes)) {}

    bool valid() const { return !m_bytes.empty(); }

    const Bytes& bytes() const { return m_bytes; }
private:
    Bytes m_bytes;
};

class ISignatureBuilder
{
public:
    virtual ~ISignatureBuilder() = default;

    virtual void update(BytesView bytes) = 0;
    virtual Signature finalize() = 0;
};

class ISignatureVerifier
{
public:
    virtual ~ISignatureVerifier() = default;

    virtual void update(BytesView bytes) = 0;
    virtual bool finalize(const Signature& signature) = 0;
};

using ISignatureBuilderPtr = std::unique_ptr<ISignatureBuilder>;
using ISignatureVerifierPtr = std::unique_ptr<ISignatureVerifier>;

ISignatureBuilderPtr create_signature_builder(HashType hash_type, const KeyPair& key_pair);
ISignatureVerifierPtr create_signature_verifier(HashType hash_type, const KeyPair& key_pair);


} // namespace Slic3r::Biz::Crypto
