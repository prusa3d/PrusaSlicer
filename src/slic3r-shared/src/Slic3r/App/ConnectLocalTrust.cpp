#include "Slic3r/App/ConnectLocalTrust.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Network/ServerTrustStore.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>

#include <iterator>
#include <optional>

namespace Slic3r::App::ConnectLocalTrust {

using Biz::Network::ServerTrust;
using Biz::Network::ServerTrustKind;
using Biz::Network::ServiceConfig;

namespace {

constexpr size_t MAX_CERTIFICATE_FILE_SIZE = 1024 * 1024;

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

std::string candidate_pem_path()
{
    return AppConfig::connect_local_trust_pem_path() + ".candidate";
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

    AppServices::instance().server_trust_store().apply(app_config.connect_local_trust());
}

} // namespace

ServerTrust stored_trust_for(const std::string& server_url)
{
    ServerTrust trust = AppServices::instance().app_config().connect_local_trust();
    if (!trust.is_custom() || trust.server_url != server_url) {
        return {};
    }
    return trust;
}

std::optional<ServerTrust>
pinned_key_trust(const std::string& server_url, const Biz::Network::CertificateInfo& certificate)
{
    if (server_url.empty() || !certificate.spki_pin.starts_with("sha256//")) {
        return std::nullopt;
    }
    ServerTrust trust;
    trust.kind       = ServerTrustKind::PinnedKey;
    trust.server_url = server_url;
    trust.pin        = certificate.spki_pin;
    return trust;
}

std::string certificate_file_trust(
    const std::string& server_url,
    const boost::filesystem::path& path,
    ServerTrust& trust
)
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
    if (!write_file(candidate_pem_path(), *pem)) {
        return Biz::_u8L("The certificate could not be stored in the PrusaSlicer data folder.");
    }
    trust            = ServerTrust{};
    trust.kind       = ServerTrustKind::Certificate;
    trust.server_url = server_url;
    trust.pem_path   = candidate_pem_path();
    trust.pem        = *pem;
    return {};
}

bool store_trust(const ServerTrust& trust)
{
    if (trust.kind == ServerTrustKind::Certificate) {
        boost::system::error_code ec;
        boost::filesystem::rename(
            candidate_pem_path(),
            AppConfig::connect_local_trust_pem_path(),
            ec
        );
        if (ec) {
            SPDLOG_ERROR("Could not store the Connect Local certificate: {}", ec.message());
            return false;
        }
    } else {
        remove_certificate_file();
    }
    store(trust.kind, trust.server_url, trust.pin);
    SPDLOG_INFO("Connect Local server {}: certificate trust stored.", trust.server_url);
    return true;
}

void discard_candidate_trust()
{
    boost::system::error_code ec;
    boost::filesystem::remove(candidate_pem_path(), ec);
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
    const AppConfig& app_config = AppServices::instance().app_config();
    const std::string server    = app_config.connect_local_server().value_or(std::string());
    if (!Biz::Network::ServerTrustStore::keeps_trust(app_config.connect_local_trust(), server)) {
        forget();
    }
}

} // namespace Slic3r::App::ConnectLocalTrust
