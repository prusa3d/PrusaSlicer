#pragma once

#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/Tls.hpp"

#include <boost/filesystem/path.hpp>

#include <optional>
#include <string>

/**
 * @brief The certificate trust of the Connect Local server as stored in the app config (and the
 * data directory). Every change is applied to the server trust store of AppServices right away.
 */
namespace Slic3r::App::ConnectLocalTrust {

/**
 * @brief The trust stored in the app config if it belongs to the server, else the system trust.
 * @param server_url A normalized server URL.
 */
Biz::Network::ServerTrust stored_trust_for(const std::string& server_url);

/**
 * @brief The trust that pins the public key of a certificate a server presented. Nothing is
 * stored, see store_trust().
 * @return nullopt when the certificate cannot be pinned.
 */
std::optional<Biz::Network::ServerTrust>
pinned_key_trust(const std::string& server_url, const Biz::Network::CertificateInfo& certificate);

/**
 * @brief The trust that verifies the server with the certificate(s) of a PEM or DER file (a CA
 * or the server's own certificate). The certificates are kept in a candidate file in the data
 * directory until store_trust() or discard_candidate_trust(); the stored trust is untouched.
 * @return Empty on success, else a translated error.
 */
std::string certificate_file_trust(
    const std::string& server_url,
    const boost::filesystem::path& path,
    Biz::Network::ServerTrust& trust
);

/**
 * @brief Makes a trust of pinned_key_trust() or certificate_file_trust() the stored one and
 * applies it. Call it once the server was checked with that trust.
 * @return False when the certificate cannot be stored in the data directory.
 */
bool store_trust(const Biz::Network::ServerTrust& trust);

/**
 * @brief Removes the candidate file of certificate_file_trust().
 */
void discard_candidate_trust();

/**
 * @brief Drops the stored trust (System again) and its certificate file.
 */
void forget();

/**
 * @brief forget() unless ServerTrustStore::keeps_trust() with the configured server of
 * the app config.
 */
void forget_if_stale();

} // namespace Slic3r::App::ConnectLocalTrust
