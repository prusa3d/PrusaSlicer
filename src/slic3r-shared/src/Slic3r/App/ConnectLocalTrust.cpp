#include "Slic3r/App/ConnectLocalTrust.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Network/HttpCurl.hpp"
#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Log.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>

#include <functional>
#include <iterator>
#include <locale>
#include <mutex>

namespace Slic3r::App::ConnectLocalTrust {

using Biz::Network::ServerTrust;
using Biz::Network::ServerTrustKind;
using Biz::Network::ServiceConfig;

namespace {

constexpr size_t MAX_CERTIFICATE_FILE_SIZE = 1024 * 1024;

/** @brief The trust in effect, see apply(). */
std::mutex applied_mutex;
ServerTrust applied;
/** @brief See set_apply_listener(), guarded by applied_mutex. */
std::function<void()> apply_listener;

std::optional<std::string> read_file(const boost::filesystem::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool write_file(const std::string& path, const std::string& contents)
{
    const boost::filesystem::path target(path);
    const boost::filesystem::path temp = target.string() + ".tmp";
    {
        boost::nowide::ofstream file(temp.string(), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file) {
            return false;
        }
    }
    boost::system::error_code ec;
    boost::filesystem::rename(temp, target, ec);
    if (ec) {
        SPDLOG_ERROR("Could not store the Connect Local certificate {}: {}", path, ec.message());
        boost::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

void remove_certificate_file()
{
    boost::system::error_code ec;
    boost::filesystem::remove(AppConfig::connect_local_trust_pem_path(), ec);
}

/** @brief Writes the trust to the app config and applies it. */
void store(ServerTrustKind kind, const std::string& server_url, const std::string& pin)
{
    AppConfig& app_config                      = AppServices::instance().app_config();
    AppConfigInteractor& app_config_interactor = AppServices::instance().app_config_interactor();

    app_config_interactor
        .set_item_value("connect_local_trust_url", Domain::ConfigValue(server_url));
    app_config_interactor.set_item_value("connect_local_trust_pin", Domain::ConfigValue(pin));
    Domain::ConfigValue value =
        app_config.get_config_box().items.opt("connect_local_trust").value();
    value.set(kind);
    app_config_interactor.set_item_value("connect_local_trust", value);

    apply(app_config.connect_local_trust());
}

/** @brief The applied trust if its server is the current offline https server. */
std::optional<ServerTrust> active_trust()
{
    ServerTrust trust;
    {
        std::lock_guard lock(applied_mutex);
        trust = applied;
    }
    if (!trust.is_custom()
        || !trust.server_url.starts_with("https://")
        || ServiceConfig::instance().offline_server_url() != trust.server_url)
    {
        return std::nullopt;
    }
    return trust;
}

} // namespace

bool
keeps_trust(const ServerTrust& trust, Biz::Network::ServiceMode mode, const std::string& server_url)
{
    if (!trust.is_custom()) {
        return true;
    }
    return mode == Biz::Network::ServiceMode::Offline
        && !server_url.empty()
        && trust.server_url == server_url;
}

ServerTrust stored_trust_for(const std::string& server_url)
{
    ServerTrust trust = AppServices::instance().app_config().connect_local_trust();
    if (!trust.is_custom() || trust.server_url != server_url) {
        return {};
    }
    return trust;
}

void apply(ServerTrust trust)
{
    // Every request asks for the trust of its URL from now on.
    static std::once_flag lookup_installed;
    std::call_once(
        lookup_installed,
        []() { Biz::Network::HttpCurl::set_server_trust_lookup(&trust_for_url); }
    );

    if (trust.kind == ServerTrustKind::Certificate && trust.pem.empty()) {
        trust.pem = read_file(trust.pem_path).value_or(std::string());
        if (trust.pem.empty()) {
            SPDLOG_ERROR("Connect Local certificate file {} cannot be read.", trust.pem_path);
        }
    }
    std::function<void()> listener;
    {
        std::lock_guard lock(applied_mutex);
        applied  = std::move(trust);
        listener = apply_listener;
    }
    if (listener) {
        listener();
    }
}

void set_apply_listener(std::function<void()> listener)
{
    std::lock_guard lock(applied_mutex);
    apply_listener = std::move(listener);
}

std::optional<ServerTrust> trust_for_url(const std::string& url)
{
    std::optional<ServerTrust> trust = active_trust();
    if (!trust || !Biz::Network::url_belongs_to_server(url, trust->server_url)) {
        return std::nullopt;
    }
    return trust;
}

std::optional<ServerTrust> trust_for_host(std::string_view host, int port)
{
    std::optional<ServerTrust> trust = active_trust();
    if (!trust) {
        return std::nullopt;
    }
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    const std::optional<Biz::Network::UrlParts> server = Biz::Network::parse_url(trust->server_url);
    const std::string lower_host =
        boost::algorithm::to_lower_copy(std::string(host), std::locale::classic());
    if (!server || server->port != port || server->host != lower_host) {
        return std::nullopt;
    }
    return trust;
}

bool
trust_certificate(const std::string& server_url, const Biz::Network::CertificateInfo& certificate)
{
    if (server_url.empty() || !certificate.spki_pin.starts_with("sha256//")) {
        return false;
    }
    // The pin is the whole trust, a certificate file loaded before is of no use any more.
    remove_certificate_file();
    store(ServerTrustKind::PinnedKey, server_url, certificate.spki_pin);
    SPDLOG_INFO(
        "Connect Local server {}: trusting the certificate {} (key {})",
        server_url,
        certificate.sha256_fingerprint,
        certificate.spki_pin
    );
    return true;
}

std::string
load_certificate_file(const std::string& server_url, const boost::filesystem::path& path)
{
    boost::system::error_code ec;
    const boost::uintmax_t size = boost::filesystem::file_size(path, ec);
    const std::optional<std::string> contents =
        ec || size == 0 || size > MAX_CERTIFICATE_FILE_SIZE ? std::nullopt : read_file(path);
    if (!contents) {
        return Biz::_u8L("The file could not be read or it is not a certificate.");
    }
    const std::optional<std::string> pem = Biz::Network::Tls::certificates_to_pem(*contents);
    if (!pem) {
        return Biz::_u8L("The file does not contain a certificate in PEM or DER format.");
    }
    if (!write_file(AppConfig::connect_local_trust_pem_path(), *pem)) {
        return Biz::_u8L("The certificate could not be stored in the PrusaSlicer data folder.");
    }
    store(ServerTrustKind::Certificate, server_url, std::string());
    SPDLOG_INFO(
        "Connect Local server {}: verifying with the certificate from {}",
        server_url,
        path.string()
    );
    return {};
}

void forget()
{
    const AppConfig& app_config = AppServices::instance().app_config();
    if (app_config.connect_local_trust().is_custom()
        || !app_config.get<std::string>("connect_local_trust_url").empty())
    {
        store(ServerTrustKind::System, std::string(), std::string());
        SPDLOG_INFO("Connect Local server certificate trust forgotten.");
    }
    remove_certificate_file();
}

void forget_if_stale()
{
    const AppConfig& app_config          = AppServices::instance().app_config();
    const Biz::Network::ServiceMode mode = app_config.service_mode();
    const std::string server             = mode == Biz::Network::ServiceMode::Offline ?
        ServiceConfig::normalize_offline_server_url(app_config.connect_local_url())
            .value_or(std::string()) :
        std::string();
    if (!keeps_trust(app_config.connect_local_trust(), mode, server)) {
        forget();
    }
}

} // namespace Slic3r::App::ConnectLocalTrust
