#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::Network {

/**
 * @brief How the certificate of the Connect Local (Connect Local) server is checked.
 *
 * Certificate checking is never turned off. System uses the system trust store, as any other
 * https request. PinnedKey accepts exactly one server public key, whatever the host name in the
 * certificate (the server is often reached through an IP address or a .local name).
 * Certificate verifies the server certificate against a user loaded CA (or server)
 * certificate, host name included.
 */
enum class ServerTrustKind
{
    System,
    PinnedKey,
    Certificate,
};

/**
 * @brief Trust chosen by the user for one Connect Local server.
 */
struct ServerTrust
{
    ServerTrustKind kind{ServerTrustKind::System};
    /**
     * @brief Normalized server URL (ServiceConfig::normalize_connect_local_server_url) the trust belongs
     * to.
     */
    std::string server_url;
    /**
     * @brief PinnedKey: "sha256//<base64 SHA-256 of the SubjectPublicKeyInfo>", the curl pin
     * format.
     */
    std::string pin;
    /**
     * @brief Certificate: the file with the loaded CA or server certificate(s), PEM
     * (CURLOPT_CAINFO).
     */
    std::string pem_path;
    /** @brief Certificate: the contents of pem_path, for the checks outside of curl (web views). */
    std::string pem;

    bool is_custom() const
    {
        return kind != ServerTrustKind::System;
    }

    /**
     * @brief Whether a certificate chain presented by the server is accepted.
     *
     * Used where the network stack is not curl (web views).
     * PinnedKey: the public key of the leaf equals the pin, nothing else is checked.
     * Certificate: Tls::chain_verifies() against pem.
     * System: false, the platform's own verification is used.
     * @param chain Leaf certificate first, then the intermediates, each PEM or DER.
     * @param host Host name or IP address the server was reached at.
     */
    bool accepts_chain(const std::vector<std::string>& chain, std::string_view host) const;

    bool operator==(const ServerTrust&) const = default;
};

} // namespace Slic3r::Biz::Network
