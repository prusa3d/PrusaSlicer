#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::Network {

/**
 * @brief Human readable summary of a certificate, shown before the user trusts it.
 */
struct CertificateInfo
{
    /** @brief SHA-256 of the DER certificate, upper case hex bytes separated by colons. */
    std::string sha256_fingerprint;
    /** @brief curl pin of the certificate's public key, see Tls::spki_pin(). */
    std::string spki_pin;
    std::string subject;
    std::string issuer;
    /** @brief DNS names and IP addresses of the subjectAltName extension. */
    std::vector<std::string> subject_alt_names;
    /** @brief "YYYY-MM-DD HH:MM:SS UTC". */
    std::string not_before;
    std::string not_after;
    bool expired{false};
    bool not_yet_valid{false};
    bool self_signed{false};
};

/**
 * @brief X.509 certificate utilities (OpenSSL), for checking servers outside of curl.
 */
namespace Tls {

/**
 * @brief Converts the contents of a certificate file to PEM.
 * @param contents One or more PEM certificates, or a single DER certificate.
 * @return All certificates found, PEM encoded, or nullopt when there is none.
 */
std::optional<std::string> certificates_to_pem(std::string_view contents);

/**
 * @brief Summary of the first certificate in PEM or DER data, nullopt if it cannot be parsed.
 */
std::optional<CertificateInfo> certificate_info(std::string_view pem_or_der);

/**
 * @brief "sha256//<base64>" pin (the curl format) of the public key of the first certificate in
 * PEM or DER data.
 */
std::optional<std::string> spki_pin(std::string_view pem_or_der);

/**
 * @brief Upper case hex bytes separated by colons, e.g. "0A:FF:12".
 */
std::string format_fingerprint(const unsigned char* data, size_t size);

/**
 * @brief Verifies a certificate chain presented by a server against CA certificates.
 *
 * Verifies the signatures, validity and the host name (or IP address); any certificate of ca_pem
 * is a trust anchor, also one that is not self-signed (e.g. the server certificate itself).
 * @param chain Leaf certificate first, then the intermediates, each PEM or DER.
 * @param ca_pem The trusted certificates, PEM.
 * @param host Host name or IP address the chain must be valid for (IPv6 with or without
 * brackets).
 */
bool chain_verifies(
    const std::vector<std::string>& chain,
    std::string_view ca_pem,
    std::string_view host
);

} // namespace Tls

} // namespace Slic3r::Biz::Network
