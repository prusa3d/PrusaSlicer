#include "Slic3r/Biz/Network/Tls.hpp"

#include "Slic3r/Log.hpp"

#include <fmt/format.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <ctime>
#include <memory>

namespace Slic3r::Biz::Network {

namespace {

struct X509Deleter
{
    void operator()(X509* p) const
    {
        X509_free(p);
    }
};

struct BioDeleter
{
    void operator()(BIO* p) const
    {
        BIO_free(p);
    }
};

struct StoreDeleter
{
    void operator()(X509_STORE* p) const
    {
        X509_STORE_free(p);
    }
};

struct StoreCtxDeleter
{
    void operator()(X509_STORE_CTX* p) const
    {
        X509_STORE_CTX_free(p);
    }
};

struct StackDeleter
{
    void operator()(STACK_OF(X509) * p) const
    {
        sk_X509_pop_free(p, X509_free);
    }
};

struct NamesDeleter
{
    void operator()(GENERAL_NAMES* p) const
    {
        GENERAL_NAMES_free(p);
    }
};

using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using BioPtr  = std::unique_ptr<BIO, BioDeleter>;

constexpr std::string_view PEM_CERTIFICATE_HEADER = "-----BEGIN CERTIFICATE-----";

BioPtr memory_bio(std::string_view data)
{
    return BioPtr(BIO_new_mem_buf(data.data(), static_cast<int>(data.size())));
}

std::vector<X509Ptr> read_certificates(std::string_view data)
{
    std::vector<X509Ptr> result;
    if (data.empty()) {
        return result;
    }
    if (data.find(PEM_CERTIFICATE_HEADER) != std::string_view::npos) {
        BioPtr bio = memory_bio(data);
        while (bio) {
            X509* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
            if (cert == nullptr) {
                break;
            }
            result.emplace_back(cert);
        }
        return result;
    }
    const unsigned char* der = reinterpret_cast<const unsigned char*>(data.data());
    if (X509* cert = d2i_X509(nullptr, &der, static_cast<long>(data.size()))) {
        result.emplace_back(cert);
    }
    return result;
}

X509Ptr read_first_certificate(std::string_view data)
{
    std::vector<X509Ptr> certificates = read_certificates(data);
    if (certificates.empty()) {
        return {};
    }
    return std::move(certificates.front());
}

std::string bio_to_string(BIO* bio)
{
    char* data      = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    return size > 0 && data != nullptr ?
        std::string(data, static_cast<size_t>(size)) :
        std::string();
}

std::string to_pem(X509* cert)
{
    BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio || PEM_write_bio_X509(bio.get(), cert) != 1) {
        return {};
    }
    return bio_to_string(bio.get());
}

std::string name_to_string(const X509_NAME* name)
{
    BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio || name == nullptr) {
        return {};
    }
    // RFC 2253 ("CN=host,O=Org"), UTF-8 kept as is.
    if (X509_NAME_print_ex(bio.get(), name, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB) < 0) {
        return {};
    }
    return bio_to_string(bio.get());
}

std::string time_to_string(const ASN1_TIME* time)
{
    std::tm tm{};
    if (time == nullptr || ASN1_TIME_to_tm(time, &tm) != 1) {
        return {};
    }
    return fmt::format(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );
}

std::string ip_to_string(const unsigned char* data, int size)
{
    if (size == 4) {
        return fmt::format("{}.{}.{}.{}", data[0], data[1], data[2], data[3]);
    }
    if (size == 16) {
        std::string out;
        for (int i = 0; i < 16; i += 2) {
            if (i > 0) {
                out += ':';
            }
            out += fmt::format("{:x}", (unsigned(data[i]) << 8) | data[i + 1]);
        }
        return out;
    }
    return {};
}

std::vector<std::string> subject_alt_names(X509* cert)
{
    std::vector<std::string> result;
    std::unique_ptr<GENERAL_NAMES, NamesDeleter> names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr))
    );
    if (!names) {
        return result;
    }
    for (int i = 0; i < sk_GENERAL_NAME_num(names.get()); ++i) {
        const GENERAL_NAME* name = sk_GENERAL_NAME_value(names.get(), i);
        if (name->type == GEN_DNS) {
            const ASN1_STRING* value = name->d.dNSName;
            result.emplace_back(
                reinterpret_cast<const char*>(ASN1_STRING_get0_data(value)),
                static_cast<size_t>(ASN1_STRING_length(value))
            );
        } else if (name->type == GEN_IPADD) {
            const ASN1_STRING* value = name->d.iPAddress;
            std::string ip = ip_to_string(ASN1_STRING_get0_data(value), ASN1_STRING_length(value));
            if (!ip.empty()) {
                result.emplace_back(std::move(ip));
            }
        }
    }
    return result;
}

std::optional<std::string> sha256_base64_of_spki(X509* cert)
{
    const X509_PUBKEY* pubkey = X509_get_X509_PUBKEY(cert);
    if (pubkey == nullptr) {
        return std::nullopt;
    }
    unsigned char* der = nullptr;
    const int der_len  = i2d_X509_PUBKEY(pubkey, &der);
    if (der_len <= 0 || der == nullptr) {
        return std::nullopt;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    const int ok =
        EVP_Digest(der, static_cast<size_t>(der_len), digest, &digest_len, EVP_sha256(), nullptr);
    OPENSSL_free(der);
    if (ok != 1) {
        return std::nullopt;
    }
    // Base64 of 32 bytes is 44 characters plus the terminating zero.
    unsigned char encoded[64] = {};
    const int encoded_len     = EVP_EncodeBlock(encoded, digest, static_cast<int>(digest_len));
    if (encoded_len <= 0) {
        return std::nullopt;
    }
    return "sha256//"
        + std::string(reinterpret_cast<const char*>(encoded), static_cast<size_t>(encoded_len));
}

} // namespace

namespace Tls {

std::optional<std::string> certificates_to_pem(std::string_view contents)
{
    std::vector<X509Ptr> certificates = read_certificates(contents);
    std::string pem;
    for (const X509Ptr& cert : certificates) {
        pem += to_pem(cert.get());
    }
    if (pem.empty()) {
        return std::nullopt;
    }
    return pem;
}

std::string format_fingerprint(const unsigned char* data, size_t size)
{
    std::string out;
    out.reserve(size * 3);
    for (size_t i = 0; i < size; ++i) {
        if (i > 0) {
            out += ':';
        }
        out += fmt::format("{:02X}", data[i]);
    }
    return out;
}

std::optional<std::string> spki_pin(std::string_view pem_or_der)
{
    X509Ptr cert = read_first_certificate(pem_or_der);
    if (!cert) {
        return std::nullopt;
    }
    return sha256_base64_of_spki(cert.get());
}

std::optional<CertificateInfo> certificate_info(std::string_view pem_or_der)
{
    X509Ptr cert = read_first_certificate(pem_or_der);
    if (!cert) {
        return std::nullopt;
    }

    CertificateInfo info;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (X509_digest(cert.get(), EVP_sha256(), digest, &digest_len) != 1) {
        return std::nullopt;
    }
    info.sha256_fingerprint = format_fingerprint(digest, digest_len);

    std::optional<std::string> pin = sha256_base64_of_spki(cert.get());
    if (!pin) {
        return std::nullopt;
    }
    info.spki_pin = std::move(*pin);

    info.subject           = name_to_string(X509_get_subject_name(cert.get()));
    info.issuer            = name_to_string(X509_get_issuer_name(cert.get()));
    info.subject_alt_names = subject_alt_names(cert.get());
    info.not_before        = time_to_string(X509_get0_notBefore(cert.get()));
    info.not_after         = time_to_string(X509_get0_notAfter(cert.get()));
    info.not_yet_valid     = X509_cmp_current_time(X509_get0_notBefore(cert.get())) > 0;
    info.expired           = X509_cmp_current_time(X509_get0_notAfter(cert.get())) < 0;
    info.self_signed       = X509_check_issued(cert.get(), cert.get()) == X509_V_OK;
    return info;
}

bool chain_verifies(
    const std::vector<std::string>& chain,
    std::string_view ca_pem,
    std::string_view host
)
{
    if (chain.empty() || host.empty()) {
        return false;
    }
    X509Ptr leaf = read_first_certificate(chain.front());
    if (!leaf) {
        return false;
    }

    std::unique_ptr<X509_STORE, StoreDeleter> store(X509_STORE_new());
    if (!store) {
        return false;
    }
    std::vector<X509Ptr> anchors = read_certificates(ca_pem);
    if (anchors.empty()) {
        return false;
    }
    for (const X509Ptr& anchor : anchors) {
        X509_STORE_add_cert(store.get(), anchor.get());
    }

    std::unique_ptr<STACK_OF(X509), StackDeleter> untrusted(sk_X509_new_null());
    if (!untrusted) {
        return false;
    }
    for (size_t i = 1; i < chain.size(); ++i) {
        if (X509Ptr cert = read_first_certificate(chain[i])) {
            if (sk_X509_push(untrusted.get(), cert.get()) > 0) {
                cert.release();
            }
        }
    }

    std::unique_ptr<X509_STORE_CTX, StoreCtxDeleter> ctx(X509_STORE_CTX_new());
    if (!ctx || X509_STORE_CTX_init(ctx.get(), store.get(), leaf.get(), untrusted.get()) != 1) {
        return false;
    }
    X509_STORE_CTX_set_purpose(ctx.get(), X509_PURPOSE_SSL_SERVER);
    X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(ctx.get());
    // A loaded certificate is an anchor even when it is not self-signed (an intermediate or the
    // server certificate itself), as curl does with CURLOPT_CAINFO.
    X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_PARTIAL_CHAIN);

    std::string host_name(host);
    if (host_name.size() > 2 && host_name.front() == '[' && host_name.back() == ']') {
        host_name = host_name.substr(1, host_name.size() - 2);
    }
    if (X509_VERIFY_PARAM_set1_ip_asc(param, host_name.c_str()) != 1) {
        if (X509_VERIFY_PARAM_set1_host(param, host_name.c_str(), host_name.size()) != 1) {
            return false;
        }
    }

    const int ok = X509_verify_cert(ctx.get());
    if (ok != 1) {
        SPDLOG_WARN(
            "Server certificate rejected by the loaded certificate: {}",
            X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx.get()))
        );
    }
    return ok == 1;
}

} // namespace Tls

} // namespace Slic3r::Biz::Network
