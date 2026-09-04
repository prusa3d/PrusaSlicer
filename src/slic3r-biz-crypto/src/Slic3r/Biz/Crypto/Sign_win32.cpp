#include "Slic3r/Biz/Crypto/Sign.hpp"
#include "Slic3r/Biz/Crypto/SecureString.hpp"
#include "Slic3r/Assert.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cwchar>

#include <fmt/format.h>

namespace Slic3r::Biz::Crypto {

namespace Internal {
struct KeyImpl
{
    BCRYPT_KEY_HANDLE handle_key{nullptr};
    BCRYPT_ALG_HANDLE handle_alg{nullptr};

    KeyImpl() = default;

    ~KeyImpl()
    {
        if (handle_key)
            BCryptDestroyKey(handle_key);
        if (handle_alg)
            BCryptCloseAlgorithmProvider(handle_alg, 0);
    }

    // Move semantics required
    KeyImpl(KeyImpl&& other) noexcept
    {
        handle_key       = other.handle_key;
        other.handle_key = nullptr;
        handle_alg       = other.handle_alg;
        other.handle_alg = nullptr;
    }

    KeyImpl& operator=(KeyImpl&& other) noexcept
    {
        if (this != &other) {
            if (handle_key)
                BCryptDestroyKey(handle_key);
            if (handle_alg)
                BCryptCloseAlgorithmProvider(handle_alg, 0);
            handle_key       = other.handle_key;
            other.handle_key = nullptr;
            handle_alg       = other.handle_alg;
            other.handle_alg = nullptr;
        }
        return *this;
    }
};
} // namespace Internal

namespace {
template <class T>
struct SecureAllocator
{
    using value_type = T;

    SecureAllocator() = default;
    template <class U> constexpr SecureAllocator(const SecureAllocator<U>&) noexcept {}

    T* allocate(std::size_t n)
    {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc();
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept
    {
        secure_wipe(p, n * sizeof(T));
        ::operator delete(p);
    }

    template <class U> bool operator==(const SecureAllocator<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const SecureAllocator<U>&) const noexcept { return false; }
};

using SecureBuffer = std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>;
// -----------------------------------------------------------------------------
// Buffers that wipe themselves on every deallocation, including reallocation.
// -----------------------------------------------------------------------------
using SecureBytes = std::vector<BYTE, SecureAllocator<BYTE>>;
using DerBuf      = SecureBytes;

// -----------------------------------------------------------------------------
// Minimal DER writer (only what RSAPublicKey / RSAPrivateKey / PKCS#8 need)
// -----------------------------------------------------------------------------

// AlgorithmIdentifier { rsaEncryption (1.2.840.113549.1.1.1), NULL }
constexpr BYTE k_alg_id_rsa[] =
    {0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00};

void der_put_len(DerBuf& out, size_t len)
{
    if (len < 0x80) {
        out.push_back(static_cast<BYTE>(len));
        return;
    }
    BYTE tmp[sizeof(size_t)];
    int n = 0;
    while (len) {
        tmp[n++] = static_cast<BYTE>(len & 0xFF);
        len >>= 8;
    }
    out.push_back(static_cast<BYTE>(0x80 | n));
    for (int i = n - 1; i >= 0; --i)
        out.push_back(tmp[i]);
}

void der_put_tlv(DerBuf& out, BYTE tag, const BYTE* data, size_t len)
{
    out.push_back(tag);
    der_put_len(out, len);
    out.insert(out.end(), data, data + len);
}

DerBuf der_wrap(BYTE tag, const DerBuf& content)
{
    DerBuf out;
    der_put_tlv(out, tag, content.data(), content.size());
    return out;
}

// CNG stores components as unsigned big-endian; DER INTEGER is signed.
void der_put_uint(DerBuf& out, const BYTE* data, size_t len)
{
    if (len == 0) {
        const BYTE zero = 0x00;
        der_put_tlv(out, 0x02, &zero, 1);
        return;
    }
    size_t i = 0;
    while (i + 1 < len && data[i] == 0x00)
        ++i; // strip redundant leading zeros

    const bool needs_pad = (data[i] & 0x80) != 0;
    out.push_back(0x02);
    der_put_len(out, (len - i) + (needs_pad ? 1 : 0));
    if (needs_pad)
        out.push_back(0x00);
    out.insert(out.end(), data + i, data + len);
}

// -----------------------------------------------------------------------------
// CNG helpers
// -----------------------------------------------------------------------------
bool is_rsa_key(BCRYPT_KEY_HANDLE handle_key)
{
    WCHAR name[64]{};
    ULONG cb_result = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            handle_key,
            BCRYPT_ALGORITHM_NAME,
            (PUCHAR) name,
            sizeof(name),
            &cb_result,
            0
        )))
    {
        return false;
    }
    name[63] = L'\0'; // BCryptGetProperty is not required to NUL-terminate on overflow
    return std::wcscmp(name, BCRYPT_RSA_ALGORITHM) == 0;
}

SecureBytes export_key_blob(BCRYPT_KEY_HANDLE handle_key, LPCWSTR blob_type)
{
    ULONG needed    = 0;
    NTSTATUS status = BCryptExportKey(handle_key, nullptr, blob_type, nullptr, 0, &needed, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(
            fmt::format("BCryptExportKey (size query) failed: 0x{:08X}", (unsigned) status)
        );
    }

    SecureBytes blob(needed);
    status = BCryptExportKey(handle_key, nullptr, blob_type, blob.data(), needed, &needed, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(fmt::format("BCryptExportKey failed: 0x{:08X}", (unsigned) status));
    }
    if (needed > blob.size())
        throw CryptoException("BCryptExportKey returned an out-of-range length");
    blob.resize(needed);
    return blob;
}

SecureString der_to_pem(const DerBuf& der, std::string_view label)
{
    constexpr DWORD flags      = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    constexpr size_t line_wrap = 64;

    // Size query: the returned count *includes* the terminating NUL.
    DWORD b64_capacity = 0;
    if (!CryptBinaryToStringA(der.data(), (DWORD) der.size(), flags, nullptr, &b64_capacity))
        throw CryptoException("Writing PEM failed: CryptBinaryToStringA (size query)");

    std::vector<char, SecureAllocator<char>> b64(b64_capacity);

    // In/out: in = buffer size, out = characters written *excluding* the NUL.
    DWORD b64_len = b64_capacity;
    if (!CryptBinaryToStringA(der.data(), (DWORD) der.size(), flags, b64.data(), &b64_len))
        throw CryptoException("Writing PEM failed: CryptBinaryToStringA");
    if (b64_len > b64.size())
        throw CryptoException("Writing PEM failed: base64 length out of range");

    const size_t line_count = (b64_len + line_wrap - 1) / line_wrap;
    const size_t total_size = (11 + label.size() + 6) // "-----BEGIN " + label + "-----\n"
        + b64_len
        + line_count // body + one '\n' per line
        + (9 + label.size() + 6); // "-----END "   + label + "-----\n"

    // Sized exactly up front: no reallocation, so this is the only buffer the
    // encoded key ever lives in, and the allocator wipes it on destruction.
    std::vector<char, SecureAllocator<char>> pem(total_size);

    size_t pos     = 0;
    const auto put = [&pem, &pos](std::string_view sv)
    {
        std::memcpy(pem.data() + pos, sv.data(), sv.size());
        pos += sv.size();
    };

    put("-----BEGIN ");
    put(label);
    put("-----\n");

    for (size_t offset = 0; offset < b64_len; offset += line_wrap) {
        const size_t chunk = std::min(line_wrap, static_cast<size_t>(b64_len) - offset);
        put(std::string_view{b64.data() + offset, chunk});
        put("\n");
    }

    put("-----END ");
    put(label);
    put("-----\n");

    if (pos != total_size)
        throw CryptoException("Writing PEM failed: internal size mismatch");

    return SecureString{pem.data(), pos};
}

// RAII for CRYPT_DECODE_ALLOC_FLAG allocations
struct LocalFreeDeleter
{
    void operator()(void* p) const noexcept
    {
        if (p)
            LocalFree(p);
    }
};

using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

SecureBytes pem_to_der(BytesView bytes)
{
    const auto* pem     = reinterpret_cast<LPCSTR>(bytes.data());
    const DWORD pem_len = static_cast<DWORD>(bytes.size());

    DWORD der_len = 0;
    if (!CryptStringToBinaryA(
            pem,
            pem_len,
            CRYPT_STRING_BASE64HEADER,
            nullptr,
            &der_len,
            nullptr,
            nullptr
        ))
    {
        throw CryptoException("Reading PEM failed: CryptStringToBinaryA (size query)");
    }

    SecureBytes der(der_len);
    if (!CryptStringToBinaryA(
            pem,
            pem_len,
            CRYPT_STRING_BASE64HEADER,
            der.data(),
            &der_len,
            nullptr,
            nullptr
        ))
    {
        throw CryptoException("Reading PEM failed: CryptStringToBinaryA");
    }
    if (der_len > der.size())
        throw CryptoException("Reading PEM failed: DER length out of range");
    der.resize(der_len);
    return der;
}

// Accepts PKCS#8 (BEGIN PRIVATE KEY) or PKCS#1 (BEGIN RSA PRIVATE KEY) DER and
// returns a legacy CAPI PRIVATEKEYBLOB, which CNG can import directly.
SecureBytes rsa_der_to_legacy_private_blob(const SecureBytes& der)
{
    const BYTE* pkcs1 = der.data();
    DWORD pkcs1_len   = static_cast<DWORD>(der.size());

    // Try PKCS#8 first; if it isn't PKCS#8 we fall through and treat the input
    // as a bare PKCS#1 RSAPrivateKey.
    void* info_ptr = nullptr;
    DWORD info_len = 0;
    LocalPtr info_guard;
    if (CryptDecodeObjectEx(
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            PKCS_PRIVATE_KEY_INFO,
            der.data(),
            static_cast<DWORD>(der.size()),
            CRYPT_DECODE_ALLOC_FLAG,
            nullptr,
            &info_ptr,
            &info_len
        ))
    {
        info_guard.reset(info_ptr);
        const auto* info = static_cast<const CRYPT_PRIVATE_KEY_INFO*>(info_ptr);
        if (!info->Algorithm.pszObjId || std::strcmp(info->Algorithm.pszObjId, szOID_RSA_RSA) != 0)
            throw CryptoException("Reading PEM failed: only RSA private keys are supported");

        pkcs1     = info->PrivateKey.pbData;
        pkcs1_len = info->PrivateKey.cbData;
    }

    void* blob_ptr     = nullptr;
    DWORD blob_len     = 0;
    const bool decoded = CryptDecodeObjectEx(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        PKCS_RSA_PRIVATE_KEY,
        pkcs1,
        pkcs1_len,
        CRYPT_DECODE_ALLOC_FLAG,
        nullptr,
        &blob_ptr,
        &blob_len
    );

    // The PKCS#8 wrapper holds the plaintext PKCS#1 key; wipe it either way.
    if (info_guard) {
        const auto* info = static_cast<const CRYPT_PRIVATE_KEY_INFO*>(info_guard.get());
        secure_wipe(info->PrivateKey.pbData, info->PrivateKey.cbData);
    }

    if (!decoded)
        throw CryptoException("Reading PEM failed: CryptDecodeObjectEx (PKCS_RSA_PRIVATE_KEY)");

    LocalPtr blob_guard(blob_ptr);

    SecureBytes
        blob(static_cast<const BYTE*>(blob_ptr), static_cast<const BYTE*>(blob_ptr) + blob_len);
    secure_wipe(blob_ptr, blob_len);
    return blob;
}

// -----------------------------------------------------------------------------
// Core logic for CNG Builders and Verifiers
// -----------------------------------------------------------------------------

LPCWSTR get_bcrypt_hash_algo(HashType type)
{
    switch (type) {
    case HashType::SHA_1:
        return BCRYPT_SHA1_ALGORITHM;
    case HashType::SHA_256:
        return BCRYPT_SHA256_ALGORITHM;
    case HashType::SHA_512:
        return BCRYPT_SHA512_ALGORITHM;
    default:
        return nullptr;
    }
}

class CngSignatureImpl
{
protected:
    BCRYPT_ALG_HANDLE m_handle_hash_alg{nullptr};
    BCRYPT_HASH_HANDLE m_handle_hash{nullptr};
    LPCWSTR m_hash_algo_str{nullptr};
    const KeyPair& m_key;
    bool m_failed{false};

    CngSignatureImpl(HashType hash_type, const KeyPair& key) : m_key(key)
    {
        m_hash_algo_str = get_bcrypt_hash_algo(hash_type);
        if (!m_hash_algo_str
            || !BCRYPT_SUCCESS(
                BCryptOpenAlgorithmProvider(&m_handle_hash_alg, m_hash_algo_str, nullptr, 0)
            ))
        {
            m_failed = true;
            return;
        }
        if (!BCRYPT_SUCCESS(
                BCryptCreateHash(m_handle_hash_alg, &m_handle_hash, nullptr, 0, nullptr, 0, 0)
            ))
        {
            m_failed = true;
        }
    }

    ~CngSignatureImpl()
    {
        if (m_handle_hash)
            BCryptDestroyHash(m_handle_hash);
        if (m_handle_hash_alg)
            BCryptCloseAlgorithmProvider(m_handle_hash_alg, 0);
    }

    CngSignatureImpl(const CngSignatureImpl&)            = delete;
    CngSignatureImpl& operator=(const CngSignatureImpl&) = delete;

    void update_hash(BytesView bytes)
    {
        if (!m_failed && !bytes.empty()) {
            if (!BCRYPT_SUCCESS(
                    BCryptHashData(m_handle_hash, (PUCHAR) bytes.data(), (ULONG) bytes.size(), 0)
                ))
            {
                m_failed = true;
            }
        }
    }

    Bytes finalize_hash()
    {
        if (m_failed)
            return {};

        DWORD hash_size = 0;
        DWORD cb_data   = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                m_handle_hash_alg,
                BCRYPT_HASH_LENGTH,
                (PUCHAR) &hash_size,
                sizeof(hash_size),
                &cb_data,
                0
            ))
            || hash_size == 0)
        {
            m_failed = true;
            return {};
        }

        Bytes hash(hash_size);
        if (BCRYPT_SUCCESS(BCryptFinishHash(m_handle_hash, (PUCHAR) hash.data(), hash_size, 0)))
            return hash;

        m_failed = true;
        return {};
    }
};

class CngSignatureBuilder : public ISignatureBuilder, public CngSignatureImpl
{
public:
    CngSignatureBuilder(HashType hash_type, const KeyPair& key) : CngSignatureImpl(hash_type, key)
    {}

    void update(BytesView bytes) override
    {
        update_hash(bytes);
    }

    Signature finalize() override
    {
        Bytes hash_data = finalize_hash();
        if (m_failed)
            throw CryptoException("Signing failed: hashing the input failed");

        BCRYPT_PKCS1_PADDING_INFO padding_info = {m_hash_algo_str};
        DWORD sig_len                          = 0;

        NTSTATUS status = BCryptSignHash(
            m_key.internal_impl().handle_key,
            &padding_info,
            (PUCHAR) hash_data.data(),
            (ULONG) hash_data.size(),
            nullptr,
            0,
            &sig_len,
            BCRYPT_PAD_PKCS1
        );
        if (!BCRYPT_SUCCESS(status)) {
            throw CryptoException(
                fmt::format(
                    "Signing failed: BCryptSignHash (size query) 0x{:08X}",
                    (unsigned) status
                )
            );
        }

        Bytes signature(sig_len);
        status = BCryptSignHash(
            m_key.internal_impl().handle_key,
            &padding_info,
            (PUCHAR) hash_data.data(),
            (ULONG) hash_data.size(),
            (PUCHAR) signature.data(),
            sig_len,
            &sig_len,
            BCRYPT_PAD_PKCS1
        );
        if (!BCRYPT_SUCCESS(status)) {
            throw CryptoException(
                fmt::format("Signing failed: BCryptSignHash 0x{:08X}", (unsigned) status)
            );
        }

        signature.resize(sig_len); // actual length can be shorter than the upper bound
        return Signature{std::move(signature)};
    }
};

class CngSignatureVerifier : public ISignatureVerifier, public CngSignatureImpl
{
public:
    CngSignatureVerifier(HashType hash_type, const KeyPair& key) : CngSignatureImpl(hash_type, key)
    {}

    void update(BytesView bytes) override
    {
        update_hash(bytes);
    }

    bool finalize(const Signature& signature) override
    {
        Bytes hash_data = finalize_hash();
        if (m_failed)
            return false;

        BCRYPT_PKCS1_PADDING_INFO padding_info = {m_hash_algo_str};

        NTSTATUS status = BCryptVerifySignature(
            m_key.internal_impl().handle_key,
            &padding_info,
            (PUCHAR) hash_data.data(),
            (ULONG) hash_data.size(),
            (PUCHAR) signature.bytes().data(),
            (ULONG) signature.bytes().size(),
            BCRYPT_PAD_PKCS1
        );

        return BCRYPT_SUCCESS(status);
    }
};

} // namespace

KeyPair::KeyPair() : m_key(std::make_unique<Internal::KeyImpl>()) {}

KeyPair::~KeyPair() = default;

KeyPair::KeyPair(KeyPair&&) noexcept            = default;
KeyPair& KeyPair::operator=(KeyPair&&) noexcept = default;

KeyPair::KeyPair(ImplPtr&& impl) : m_key(std::move(impl)) {}

KeyPair KeyPair::generate(const char* algo, int size)
{
    if (!algo || std::strcmp(algo, "RSA") != 0)
        throw CryptoException(fmt::format("Unsupported keygen cypher {}", algo ? algo : "(null)"));

    auto impl = std::make_unique<Internal::KeyImpl>();

    NTSTATUS status =
        BCryptOpenAlgorithmProvider(&impl->handle_alg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
        throw CryptoException(fmt::format("Unsupported keygen cypher {}", algo));

    status = BCryptGenerateKeyPair(impl->handle_alg, &impl->handle_key, size, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(
            fmt::format(
                "Unsupported keygen cypher {} or its size {}: 0x{:08X}",
                algo,
                size,
                (unsigned) status
            )
        );
    }

    status = BCryptFinalizeKeyPair(impl->handle_key, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(
            fmt::format("Key generation failed: BCryptFinalizeKeyPair 0x{:08X}", (unsigned) status)
        );
    }

    return KeyPair{std::move(impl)};
}

KeyPair KeyPair::generate(int size)
{
    return generate("RSA", size);
}

std::string KeyPair::save_public_key_pem() const
{
    const BCRYPT_KEY_HANDLE handle_key = m_key->handle_key;
    if (!handle_key)
        throw CryptoException("Writing PEM failed: key is empty");
    if (!is_rsa_key(handle_key))
        throw CryptoException("Writing PEM failed: only RSA keys are supported");

    const SecureBytes blob = export_key_blob(handle_key, BCRYPT_RSAPUBLIC_BLOB);
    if (blob.size() < sizeof(BCRYPT_RSAKEY_BLOB))
        throw CryptoException("Writing PEM failed: malformed RSA public blob");

    const auto* hdr = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(blob.data());
    const size_t expected =
        sizeof(BCRYPT_RSAKEY_BLOB) + size_t{hdr->cbPublicExp} + size_t{hdr->cbModulus};
    if (blob.size() < expected)
        throw CryptoException("Writing PEM failed: truncated RSA public blob");

    const BYTE* public_exp = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    const BYTE* modulus    = public_exp + hdr->cbPublicExp;

    // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
    DerBuf rsa_public_body;
    der_put_uint(rsa_public_body, modulus, hdr->cbModulus);
    der_put_uint(rsa_public_body, public_exp, hdr->cbPublicExp);
    const DerBuf rsa_public = der_wrap(0x30, rsa_public_body);

    // SubjectPublicKeyInfo ::= SEQUENCE { AlgorithmIdentifier, BIT STRING }
    DerBuf bit_string_body;
    bit_string_body.push_back(0x00); // unused bits
    bit_string_body.insert(bit_string_body.end(), rsa_public.begin(), rsa_public.end());

    DerBuf spki_body(std::begin(k_alg_id_rsa), std::end(k_alg_id_rsa));
    der_put_tlv(spki_body, 0x03, bit_string_body.data(), bit_string_body.size());

    // Public material, so unwrapping the SecureString here is fine.
    return der_to_pem(der_wrap(0x30, spki_body), "PUBLIC KEY").to_unprotected_string();
}

SecureString KeyPair::save_private_key_pem() const
{
    const BCRYPT_KEY_HANDLE handle_key = m_key->handle_key;
    if (!handle_key)
        throw CryptoException("Writing PEM failed: key is empty");
    if (!is_rsa_key(handle_key))
        throw CryptoException("Writing PEM failed: only RSA keys are supported");

    const SecureBytes blob = export_key_blob(handle_key, BCRYPT_RSAFULLPRIVATE_BLOB);
    if (blob.size() < sizeof(BCRYPT_RSAKEY_BLOB))
        throw CryptoException("Writing PEM failed: malformed RSA private blob");

    const auto* hdr = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(blob.data());
    if (hdr->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC)
        throw CryptoException("Writing PEM failed: key has no private part");

    const size_t expected = sizeof(BCRYPT_RSAKEY_BLOB)
        + size_t{hdr->cbPublicExp}
        + 2u * size_t{hdr->cbModulus}
        + 3u * size_t{hdr->cbPrime1}
        + 2u * size_t{hdr->cbPrime2};
    if (blob.size() < expected)
        throw CryptoException("Writing PEM failed: truncated RSA private blob");

    // Layout: PublicExponent, Modulus, Prime1, Prime2, Exponent1, Exponent2,
    // Coefficient, PrivateExponent - all unsigned big-endian.
    const BYTE* public_exp  = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    const BYTE* modulus     = public_exp + hdr->cbPublicExp;
    const BYTE* prime1      = modulus + hdr->cbModulus;
    const BYTE* prime2      = prime1 + hdr->cbPrime1;
    const BYTE* exponent1   = prime2 + hdr->cbPrime2;
    const BYTE* exponent2   = exponent1 + hdr->cbPrime1;
    const BYTE* coefficient = exponent2 + hdr->cbPrime2;
    const BYTE* private_exp = coefficient + hdr->cbPrime1;

    // RSAPrivateKey ::= SEQUENCE { version, n, e, d, p, q, dp, dq, qInv }
    DerBuf rsa_private_body;
    const BYTE version_zero = 0x00;
    der_put_tlv(rsa_private_body, 0x02, &version_zero, 1);
    der_put_uint(rsa_private_body, modulus, hdr->cbModulus);
    der_put_uint(rsa_private_body, public_exp, hdr->cbPublicExp);
    der_put_uint(rsa_private_body, private_exp, hdr->cbModulus);
    der_put_uint(rsa_private_body, prime1, hdr->cbPrime1);
    der_put_uint(rsa_private_body, prime2, hdr->cbPrime2);
    der_put_uint(rsa_private_body, exponent1, hdr->cbPrime1);
    der_put_uint(rsa_private_body, exponent2, hdr->cbPrime2);
    der_put_uint(rsa_private_body, coefficient, hdr->cbPrime1);
    const DerBuf rsa_private = der_wrap(0x30, rsa_private_body);

    // PrivateKeyInfo ::= SEQUENCE { version, AlgorithmIdentifier, OCTET STRING }
    DerBuf pkcs8_body;
    der_put_tlv(pkcs8_body, 0x02, &version_zero, 1);
    pkcs8_body.insert(pkcs8_body.end(), std::begin(k_alg_id_rsa), std::end(k_alg_id_rsa));
    der_put_tlv(pkcs8_body, 0x04, rsa_private.data(), rsa_private.size());

    // Every DerBuf above (and the temporary from der_wrap) is wiped by
    // SecureAllocator::deallocate when it goes out of scope.
    return der_to_pem(der_wrap(0x30, pkcs8_body), "PRIVATE KEY");
}

KeyPair KeyPair::load_pub_pem(BytesView bytes)
{
    const SecureBytes der = pem_to_der(bytes);

    void* info_ptr = nullptr;
    DWORD info_len = 0;
    if (!CryptDecodeObjectEx(
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            X509_PUBLIC_KEY_INFO,
            der.data(),
            static_cast<DWORD>(der.size()),
            CRYPT_DECODE_ALLOC_FLAG,
            nullptr,
            &info_ptr,
            &info_len
        ))
    {
        throw CryptoException("Reading PEM failed: CryptDecodeObjectEx");
    }
    LocalPtr info_guard(info_ptr);

    auto impl = std::make_unique<Internal::KeyImpl>();
    if (!CryptImportPublicKeyInfoEx2(
            X509_ASN_ENCODING,
            static_cast<PCERT_PUBLIC_KEY_INFO>(info_ptr),
            0,
            nullptr,
            &impl->handle_key
        ))
    {
        throw CryptoException("Reading PEM failed: CryptImportPublicKeyInfoEx2");
    }

    return KeyPair{std::move(impl)};
}

KeyPair KeyPair::load_priv_pem(BytesView bytes)
{
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.substr(0, std::min<size_t>(text.size(), 128)).find("ENCRYPTED")
        != std::string_view::npos)
    {
        throw CryptoException("Reading PEM failed: encrypted private keys are not supported");
    }

    const SecureBytes der  = pem_to_der(bytes);
    const SecureBytes blob = rsa_der_to_legacy_private_blob(der);

    auto impl = std::make_unique<Internal::KeyImpl>();

    NTSTATUS status =
        BCryptOpenAlgorithmProvider(&impl->handle_alg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(
            fmt::format(
                "Reading PEM failed: BCryptOpenAlgorithmProvider 0x{:08X}",
                (unsigned) status
            )
        );
    }

    status = BCryptImportKeyPair(
        impl->handle_alg,
        nullptr,
        LEGACY_RSAPRIVATE_BLOB,
        &impl->handle_key,
        const_cast<PUCHAR>(blob.data()),
        static_cast<ULONG>(blob.size()),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException(
            fmt::format("Reading PEM failed: BCryptImportKeyPair 0x{:08X}", (unsigned) status)
        );
    }

    return KeyPair{std::move(impl)};
}

ISignatureBuilderPtr create_signature_builder(HashType hash_type, const KeyPair& key_pair)
{
    return std::make_unique<CngSignatureBuilder>(hash_type, key_pair);
}

ISignatureVerifierPtr create_signature_verifier(HashType hash_type, const KeyPair& key_pair)
{
    return std::make_unique<CngSignatureVerifier>(hash_type, key_pair);
}

} // namespace Slic3r::Biz::Crypto
