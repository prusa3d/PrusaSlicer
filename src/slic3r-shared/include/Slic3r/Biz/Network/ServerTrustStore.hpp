#pragma once

#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::Network {

/**
 * @brief Told after every ServerTrustStore::apply(), on the applying thread (the main thread):
 * the trust or the Connect Local server (ServiceConfig, applied together) may have changed. For the
 * web views, which keep their own decisions about certificates.
 */
class IServerTrustListener
{
public:
    virtual ~IServerTrustListener() = default;

    virtual void on_server_trust_applied() = 0;
};

/**
 * @brief The certificate trust in effect for the connections to the Connect Local server.
 *
 * Holds the one applied trust and answers, for every connection, whether a custom trust applies
 * to it. The trust takes effect only while its server is the Connect Local server of ServiceConfig, so
 * it can be applied before switching to that server. Thread safe.
 */
class ServerTrustStore
{
public:
    /**
     * @brief Whether a stored trust still belongs to the configured server.
     *
     * It belongs to one server: configuring another one, or none, drops it. Which service is
     * active does not matter, the trust waits for its server.
     * @param server_url The normalized configured server URL, empty when there is none.
     */
    static bool keeps_trust(const ServerTrust& trust, const std::string& server_url);

    /**
     * @brief Makes the trust the one in effect for its server. The certificate file of a
     * Certificate trust is read here, once. Tells the listeners on this thread.
     */
    void apply(ServerTrust trust);

    void add_listener(IServerTrustListener* listener);
    void remove_listener(IServerTrustListener* listener);

    /**
     * @brief The custom trust in effect for a URL: the one of the current Connect Local https server
     * when the URL is under it, else nullopt (the system trust store).
     */
    std::optional<ServerTrust> trust_for_url(const std::string& url) const;

    /**
     * @brief As trust_for_url(), for the host (IPv6 with or without brackets) and port of a TLS
     * connection. For the web views, which see a connection, not a URL.
     */
    std::optional<ServerTrust> trust_for_host(std::string_view host, int port) const;

private:
    /** @brief The applied trust if its server is the current Connect Local https server. */
    std::optional<ServerTrust> active_trust() const;

    mutable std::mutex m_mutex;
    ServerTrust m_applied;
    std::vector<IServerTrustListener*> m_listeners;
};

} // namespace Slic3r::Biz::Network
