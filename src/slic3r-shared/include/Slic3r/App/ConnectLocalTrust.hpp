#pragma once

#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/Tls.hpp"

#include <boost/filesystem/path.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

/**
 * @brief The certificate trust of the Connect Local server: stored in the app config (and the
 * data directory), and the one in effect for the connections to the server.
 */
namespace Slic3r::App::ConnectLocalTrust {

/**
 * @brief Whether a stored trust still applies once the service settings are mode / server.
 *
 * It belongs to one offline server: switching online or to another server drops it.
 * @param server_url The normalized offline server URL (empty online).
 */
bool keeps_trust(
    const Biz::Network::ServerTrust& trust,
    Biz::Network::ServiceMode mode,
    const std::string& server_url
);

/**
 * @brief The trust stored in the app config if it belongs to the server, else the system trust.
 * @param server_url A normalized server URL.
 */
Biz::Network::ServerTrust stored_trust_for(const std::string& server_url);

/**
 * @brief Makes the trust the one in effect for its server: every HTTP request (HttpCurl) and
 * every web view connection to it is checked with it.
 *
 * It takes effect only while its server is the offline server (Biz::Network::ServiceConfig),
 * so it can be applied before switching to that server. The certificate file of a Certificate
 * trust is read here, once.
 */
void apply(Biz::Network::ServerTrust trust);

/**
 * @brief Called at the end of every apply(), on its thread (the main thread): the trust or the
 * offline server (Biz::Network::ServiceConfig, applied together) may have changed. For the web
 * views, which keep their own decisions about certificates.
 */
void set_apply_listener(std::function<void()> listener);

/**
 * @brief The custom trust in effect for a URL: the one of the current offline https server when
 * the URL is under it, else nullopt (the system trust store). Thread safe.
 */
std::optional<Biz::Network::ServerTrust> trust_for_url(const std::string& url);

/**
 * @brief As trust_for_url(), for the host (IPv6 with or without brackets) and port of a TLS
 * connection. For the web views, which see a connection, not a URL. Thread safe.
 */
std::optional<Biz::Network::ServerTrust> trust_for_host(std::string_view host, int port);

/**
 * @brief Trusts the certificate a server presented by pinning its public key.
 * Stored in the app config and applied right away.
 * @return False when the certificate cannot be pinned.
 */
bool
trust_certificate(const std::string& server_url, const Biz::Network::CertificateInfo& certificate);

/**
 * @brief Uses the certificate(s) of a PEM or DER file (a CA or the server's own certificate) to
 * verify the server. Stored and applied right away.
 * @return Empty on success, else a translated error.
 */
std::string
load_certificate_file(const std::string& server_url, const boost::filesystem::path& path);

/**
 * @brief Drops the stored trust (System again) and its certificate file.
 */
void forget();

/**
 * @brief forget() unless keeps_trust() with the current service settings of the app config.
 */
void forget_if_stale();

} // namespace Slic3r::App::ConnectLocalTrust
