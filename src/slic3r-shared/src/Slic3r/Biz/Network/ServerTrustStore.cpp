#include "Slic3r/Biz/Network/ServerTrustStore.hpp"

#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Log.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <iterator>
#include <locale>

namespace Slic3r::Biz::Network {

namespace {

std::string read_file(const std::string& path)
{
    boost::nowide::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

bool ServerTrustStore::keeps_trust(const ServerTrust& trust, const std::string& server_url)
{
    if (!trust.is_custom()) {
        return true;
    }
    return !server_url.empty() && trust.server_url == server_url;
}

void ServerTrustStore::apply(ServerTrust trust)
{
    if (trust.kind == ServerTrustKind::Certificate && trust.pem.empty()) {
        trust.pem = read_file(trust.pem_path);
        if (trust.pem.empty()) {
            SPDLOG_ERROR("Connect Local certificate file {} cannot be read.", trust.pem_path);
        }
    }
    std::vector<IServerTrustListener*> listeners;
    {
        std::lock_guard lock(m_mutex);
        m_applied = std::move(trust);
        listeners = m_listeners;
    }
    for (IServerTrustListener* listener : listeners) {
        listener->on_server_trust_applied();
    }
}

void ServerTrustStore::add_listener(IServerTrustListener* listener)
{
    std::lock_guard lock(m_mutex);
    m_listeners.push_back(listener);
}

void ServerTrustStore::remove_listener(IServerTrustListener* listener)
{
    std::lock_guard lock(m_mutex);
    std::erase(m_listeners, listener);
}

std::optional<ServerTrust> ServerTrustStore::active_trust() const
{
    ServerTrust trust;
    {
        std::lock_guard lock(m_mutex);
        trust = m_applied;
    }
    if (!trust.is_custom()
        || !trust.server_url.starts_with("https://")
        || ServiceConfig::instance().connect_local_server_url() != trust.server_url)
    {
        return std::nullopt;
    }
    return trust;
}

std::optional<ServerTrust> ServerTrustStore::trust_for_url(const std::string& url) const
{
    std::optional<ServerTrust> trust = active_trust();
    if (!trust || !url_belongs_to_server(url, trust->server_url)) {
        return std::nullopt;
    }
    return trust;
}

std::optional<ServerTrust> ServerTrustStore::trust_for_host(std::string_view host, int port) const
{
    std::optional<ServerTrust> trust = active_trust();
    if (!trust) {
        return std::nullopt;
    }
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    const std::optional<UrlParts> server = parse_url(trust->server_url);
    const std::string lower_host =
        boost::algorithm::to_lower_copy(std::string(host), std::locale::classic());
    if (!server || server->port != port || server->host != lower_host) {
        return std::nullopt;
    }
    return trust;
}

} // namespace Slic3r::Biz::Network
