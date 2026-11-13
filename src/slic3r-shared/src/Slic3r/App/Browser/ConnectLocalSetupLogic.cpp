#include "Slic3r/App/Browser/ConnectLocalSetupLogic.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/ConnectLocalTrust.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/Biz/ConnectLocal/ConnectLocalDiscovery.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/JsonForScript.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <utility>

namespace Slic3r::App::Browser {

namespace {

/**
 * @brief Calls a function of the page script object (e.g. window.connectLocalSetup), if the page
 * has it.
 */
BrowserLogicCommand
call_page(std::string_view object, std::string_view function, std::string_view arguments = {})
{
    return {
        BrowserLogicCommandType::RunScript,
        fmt::format("window.{0} && window.{0}.{1}({2});", object, function, arguments)
    };
}

/**
 * @brief Calls a function of window.connectLocalSetup, see connect_local_setup.html.
 */
BrowserLogicCommand call_setup_page(std::string_view function, std::string_view arguments = {})
{
    return call_page("connectLocalSetup", function, arguments);
}

BrowserLogicCommand show_error(const std::string& error)
{
    return call_setup_page("showError", Biz::json_for_script(error));
}

BrowserLogicCommand show_checking(bool checking)
{
    return call_setup_page("checking", checking ? "true" : "false");
}

/**
 * @brief A field of a page message, nullopt (logged) when the message or the field is broken.
 */
template <typename T>
std::optional<T> parse_field(const std::string& message_data, const char* field)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains(field)) {
            return j[field].get<T>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Connect Local setup message. {}", e.what());
    }
    return std::nullopt;
}

bool is_https(const std::string& normalized_url)
{
    return normalized_url.starts_with("https://");
}

bool is_resources_page(const std::string& url, std::string_view page)
{
    return url.find("/web/" + std::string(page)) != std::string::npos;
}

/**
 * @brief Labels of the switch between Prusa Connect and Prusa Connect Local at the top of the
 * slicer's Connect tab pages, see connect_pages.js.
 */
void add_mode_texts(nlohmann::json& texts)
{
    texts["modeCloud"] = Biz::_u8L("Prusa Connect");
    // TRN Prusa Connect Local, the Prusa Connect server run on the customer's own network.
    texts["modeLocal"] = Biz::_u8L("Prusa Connect (on-premise)");
    // TRN Small label under the PRUSA CONNECT logo on the Prusa Connect (on-premise) pages.
    texts["modeTag"] = Biz::_u8L("on-premise");
    // TRN Accessible name of the switch between Prusa Connect and Prusa Connect (on-premise).
    texts["modeSwitch"] = Biz::_u8L("Connect service");
}

std::string trust_kind_label(Biz::Network::ServerTrustKind kind)
{
    switch (kind) {
    case Biz::Network::ServerTrustKind::PinnedKey:
        return Biz::_u8L("The server certificate is trusted (its key is pinned).");
    case Biz::Network::ServerTrustKind::Certificate:
        return Biz::_u8L("The server certificate is verified with a loaded certificate.");
    case Biz::Network::ServerTrustKind::System:
    default:
        return {};
    }
}

} // namespace

ConnectLocalSetupLogic::
    ConnectLocalSetupLogic(Biz::UserAccount::UserAccountInteractor& user_account, Page page) :
    m_user_account(user_account),
    m_page(std::move(page))
{}

ConnectLocalSetupLogic::~ConnectLocalSetupLogic() = default;

bool ConnectLocalSetupLogic::needs_server_setup()
{
    const Biz::Network::ServiceConfig& sc = Biz::Network::ServiceConfig::instance();
    return sc.is_offline() && sc.offline_server_url().empty();
}

bool ConnectLocalSetupLogic::wants_setup_page() const
{
    return needs_server_setup()
        || (m_setup_requested && Biz::Network::ServiceConfig::instance().is_offline());
}

void ConnectLocalSetupLogic::add_actions(Actions& actions)
{
    // Sent by the slicer's offline pages only, so registered for the Connect tab only.
    const auto add = [this, &actions](const char* name, MessageHandler handler)
    {
        actions[name] = [this, name, handler](const std::string& message_data)
        {
            if (!m_local_page) {
                SPDLOG_WARN("Ignoring {} from a page that is not ours.", name);
                return std::vector<BrowserLogicCommand>{};
            }
            return (this->*handler)(message_data);
        };
    };
    add("SET_CONNECT_LOCAL_URL", &ConnectLocalSetupLogic::on_set_url);
    add("SET_CONNECT_MODE_ONLINE", &ConnectLocalSetupLogic::on_set_online);
    add("SET_CONNECT_MODE_OFFLINE", &ConnectLocalSetupLogic::on_set_offline);
    add("SHOW_CONNECT_LOCAL_SETUP", &ConnectLocalSetupLogic::on_show_setup);
    add("CANCEL_CONNECT_LOCAL_SETUP", &ConnectLocalSetupLogic::on_cancel_setup);
    add("CONNECT_LOCAL_DISCOVERY", &ConnectLocalSetupLogic::on_discovery);
    add("TRUST_CONNECT_LOCAL_CERTIFICATE", &ConnectLocalSetupLogic::on_trust_certificate);
    add("LOAD_CONNECT_LOCAL_CERTIFICATE", &ConnectLocalSetupLogic::on_load_certificate);
    add("FORGET_CONNECT_LOCAL_CERTIFICATE", &ConnectLocalSetupLogic::on_forget_certificate);
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_page_loaded(const std::string& url)
{
    m_local_page = url.starts_with("file:");
    if (m_local_page && is_resources_page(url, SETUP_PAGE)) {
        std::vector<BrowserLogicCommand> result = {
            {BrowserLogicCommandType::RunScript, setup_page_init_script()}
        };
        // The network is searched only when the user asks for it on the page. A stopped search
        // is dropped, the page starts without its old results.
        if (m_discovery && !m_discovery->running()) {
            m_discovery.reset();
        }
        std::vector<BrowserLogicCommand> state = discovery_state_commands();
        result.insert(result.end(), state.begin(), state.end());
        if (m_certificate_setup) {
            // Opened because the server's certificate was refused: go to the certificate step.
            check_current_server_certificate();
        }
        return result;
    }

    stop_discovery();
    if (m_local_page && is_resources_page(url, LOGGED_OUT_PAGE)) {
        check_certificate_before_sign_in();
        return {{BrowserLogicCommandType::RunScript, logged_out_page_init_script()}};
    }
    if (m_local_page && is_resources_page(url, CLOUD_LOGGED_OUT_PAGE)) {
        return {{BrowserLogicCommandType::RunScript, cloud_logged_out_page_init_script()}};
    }
    return {};
}

void ConnectLocalSetupLogic::on_navigation_request(const std::string& new_url)
{
    m_local_page = m_local_page && new_url.starts_with("file:");
}

void ConnectLocalSetupLogic::set_shown(bool shown)
{
    m_shown = shown;
    if (!shown) {
        stop_discovery();
    }
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::on_certificate_error(const std::string& url)
{
    const Biz::Network::ServiceConfig& sc = Biz::Network::ServiceConfig::instance();
    // The tab loads only pages of the server or of the slicer, so a refused certificate is the
    // server's (the URL of the event may still be the previous page's on some platforms).
    if (!sc.is_offline() || !is_https(sc.offline_server_url())) {
        return {};
    }
    // The setup page explains the certificate and lets the user trust it.
    SPDLOG_WARN("Connect Local page {} refused its certificate, showing the server setup.", url);
    m_certificate_setup = true;
    return m_page.setup_page();
}

std::string ConnectLocalSetupLogic::setup_page_init_script() const
{
    std::string url = Biz::Network::ServiceConfig::instance().offline_server_url();
    if (url.empty()) {
        url = AppServices::instance().app_config().connect_local_url();
    }
    nlohmann::json texts = {
        {"title", Biz::_u8L("Prusa Connect (on-premise) server")},
        {"intro",
         Biz::_u8L(
             "Enter the address of the Prusa Connect (on-premise) server that manages your printers."
         )},
        {"address", Biz::_u8L("Server address")},
        {"discoveryTitle", Biz::_u8L("Find a server on the network")},
        {"discoveryIntro",
         Biz::_u8L(
             "PrusaSlicer can look for Prusa Connect (on-premise) servers running on your local network."
         )},
        // TRN Button that starts the search for Prusa Connect (on-premise) servers.
        {"discoveryStart", Biz::_u8L("Find servers on the network")},
        {"searching", Biz::_u8L("Searching the network…")},
        {"nothing", Biz::_u8L("No server found yet. Still searching…")},
        {"stopped", Biz::_u8L("Search stopped.")},
        {"stop", Biz::_u8L("Stop")},
        {"search", Biz::_u8L("Search again")},
        {"use", Biz::_u8L("Use")},
        {"version", Biz::_u8L("version")},
        {"placeholder", "http://192.168.1.10:8080"},
        {"connect", Biz::_u8L("Connect")},
        {"cancel", Biz::_u8L("Cancel")},
        {"checking", Biz::_u8L("Connecting to the server…")},
        {"certTitle", Biz::_u8L("The server certificate is not trusted")},
        {"certIntro",
         Biz::_u8L(
             "This system does not trust the certificate of the server, which is common for a server with a self-signed certificate. Compare the fingerprint with the one of your Prusa Connect (on-premise) server before you trust it, or load the certificate of the authority that issued it."
         )},
        {"fingerprint", Biz::_u8L("SHA-256 fingerprint")},
        {"subject", Biz::_u8L("Issued to")},
        {"names", Biz::_u8L("Names")},
        {"issuer", Biz::_u8L("Issued by")},
        {"selfSigned", Biz::_u8L("self-signed")},
        {"validity", Biz::_u8L("Valid")},
        {"expired", Biz::_u8L("expired")},
        {"notYetValid", Biz::_u8L("not yet valid")},
        {"trust", Biz::_u8L("Trust this certificate")},
        {"load", Biz::_u8L("Load certificate…")},
    };
    add_mode_texts(texts);
    const nlohmann::json init = {
        {"texts", texts},
        {"url", url},
        {"canCancel", !needs_server_setup()},
    };
    return call_setup_page("init", Biz::json_for_script(init)).data;
}

std::string ConnectLocalSetupLogic::logged_out_page_init_script() const
{
    nlohmann::json texts = {
        {"title", Biz::_u8L("Sign in to Prusa Connect (on-premise)")},
        {"intro",
         Biz::_u8L(
             "PrusaSlicer will open the sign-in page of your server in a PrusaSlicer window."
         )},
        {"login", Biz::_u8L("Sign in")},
        {"change", Biz::_u8L("Change server")},
        {"forget", Biz::_u8L("Forget certificate")},
    };
    add_mode_texts(texts);
    const std::string server = Biz::Network::ServiceConfig::instance().offline_server_url();
    const Biz::Network::ServerTrust trust = ConnectLocalTrust::stored_trust_for(server);
    const nlohmann::json init             = {
        {"texts", texts},
        // TRN {} is the address of the Prusa Connect Local server, e.g. http://192.168.1.10:8080
        {"server", fmt::format(fmt::runtime(Biz::_u8L("Server: {}")), server)},
        // A certificate trusted in PrusaSlicer for this server: it can be forgotten here.
        {"trust", trust.is_custom() ? trust_kind_label(trust.kind) : std::string()},
    };
    return call_page("connectLocalLoggedOut", "init", Biz::json_for_script(init)).data;
}

std::string ConnectLocalSetupLogic::cloud_logged_out_page_init_script()
{
    nlohmann::json texts = {
        {"title", Biz::_u8L("Sign in to Prusa Connect")},
        {"intro",
         Biz::_u8L(
             "Sign in with your Prusa Account to send prints to your printers and see their status here."
         )},
        {"login", Biz::_u8L("Sign in")},
    };
    add_mode_texts(texts);
    const nlohmann::json init = {{"texts", texts}};
    return call_page("connectLoggedOut", "init", Biz::json_for_script(init)).data;
}

// ---- LAN discovery ----

void ConnectLocalSetupLogic::start_discovery()
{
    if (!m_discovery) {
        m_discovery = std::make_unique<Biz::ConnectLocal::DiscoveryLoop>(
            Biz::Platform::PlatformServices::instance().main_thread_dispatcher(),
            [this](const std::vector<Biz::ConnectLocal::Discovered>& list)
            {
                m_page.push_commands(
                    {call_setup_page("found", Biz::ConnectLocal::discovered_list_json(list))}
                );
            },
            [this]() { m_page.push_commands(discovery_state_commands()); }
        );
    }
    m_discovery->start();
}

void ConnectLocalSetupLogic::stop_discovery()
{
    if (m_discovery) {
        m_discovery->stop();
    }
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::discovery_state_commands() const
{
    const bool started   = m_discovery != nullptr;
    const bool searching = m_discovery && m_discovery->running();
    const bool nothing   = !m_discovery || (m_discovery->empty() && m_discovery->rounds_done() > 0);
    const std::string list = Biz::ConnectLocal::discovered_list_json(
        m_discovery ? m_discovery->found() : std::vector<Biz::ConnectLocal::Discovered>{}
    );
    return {
        call_setup_page("found", list),
        call_setup_page("searching", fmt::format("{}, {}, {}", searching, nothing, started)),
    };
}

// ---- Messages of the setup and logged-out pages ----

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_set_url(const std::string& message_data)
{
    const std::optional<std::string> normalized =
        Biz::Network::ServiceConfig::normalize_offline_server_url(
            parse_field<std::string>(message_data, "url").value_or(std::string())
        );
    if (!normalized) {
        return {show_error(
            Biz::_u8L(
                "This is not a valid server address. Enter it like http://192.168.1.10:8080 or http://printfarm.local:8080."
            )
        )};
    }
    if (!is_https(*normalized)) {
        return use_server(*normalized);
    }
    // An https server is used only once its certificate is accepted, else the page shows it.
    return check_and_use_server(*normalized);
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_set_online(const std::string&)
{
    m_setup_requested = false;
    stop_discovery();
    AppServices::instance().app_config_interactor().set_service_mode(
        Biz::Network::ServiceMode::Online
    );
    return {};
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_set_offline(const std::string&)
{
    // Sent by the switch of the cloud logged-out page; the mode change reloads the Connect tab.
    AppServices::instance().app_config_interactor().set_service_mode(
        Biz::Network::ServiceMode::Offline
    );
    return {};
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_show_setup(const std::string&)
{
    if (!Biz::Network::ServiceConfig::instance().is_offline() || m_user_account.is_logged_in()) {
        return {};
    }
    m_setup_requested = true;
    return m_page.setup_page();
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_cancel_setup(const std::string&)
{
    m_setup_requested = false;
    stop_discovery();
    // Back to the page whose certificate failed, or to the logged-out page that opened the setup.
    return std::exchange(m_certificate_setup, false) ?
        m_page.home_page() :
        m_page.logged_out_page();
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::on_discovery(const std::string& message_data)
{
    if (parse_field<bool>(message_data, "enabled").value_or(false) && m_shown) {
        start_discovery();
    } else {
        stop_discovery();
    }
    return discovery_state_commands();
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::on_trust_certificate(const std::string& message_data)
{
    const std::string url = parse_field<std::string>(message_data, "url").value_or(std::string());
    const std::string fingerprint =
        parse_field<std::string>(message_data, "fingerprint").value_or(std::string());
    // Only the certificate the page shows, for the server it was shown for.
    if (!m_presented_certificate
        || url != m_presented_certificate_url
        || fingerprint != m_presented_certificate->sha256_fingerprint)
    {
        SPDLOG_WARN(
            "Ignoring TRUST_CONNECT_LOCAL_CERTIFICATE for a certificate that is not shown."
        );
        return {};
    }
    const Biz::Network::CertificateInfo cert =
        *std::exchange(m_presented_certificate, std::nullopt);
    if (!ConnectLocalTrust::trust_certificate(url, cert)) {
        return {show_error(
            Biz::_u8L("The certificate could not be stored in the PrusaSlicer data folder.")
        )};
    }
    // Check it with the trust, as the connections will (the server may have changed meanwhile).
    return check_and_use_server(url);
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::on_load_certificate(const std::string& message_data)
{
    const std::optional<std::string> normalized =
        Biz::Network::ServiceConfig::normalize_offline_server_url(
            parse_field<std::string>(message_data, "url").value_or(std::string())
        );
    if (!normalized || !is_https(*normalized)) {
        return {show_error(
            Biz::_u8L(
                "A certificate can be loaded for an https server address only, e.g. https://192.168.1.10."
            )
        )};
    }

    // Not from inside the web view's message callback: the file dialog is modal.
    std::weak_ptr<char> alive = m_alive;
    Biz::Platform::PlatformServices::instance()
        .main_thread_dispatcher()
        .dispatch_on_main_thread_after(
            [this, alive, server = *normalized]()
            {
                if (alive.expired()) {
                    return;
                }
                IDialogManager::FileCallback callback =
                    [this,
                     alive,
                     server](bool ok, const std::vector<boost::filesystem::path>& paths)
                {
                    if (alive.expired() || !ok || paths.empty()) {
                        return;
                    }
                    const std::string error =
                        ConnectLocalTrust::load_certificate_file(server, paths.front());
                    m_page.push_commands(
                        error.empty() ?
                            check_and_use_server(server) :
                            std::vector<BrowserLogicCommand>{show_error(error)}
                    );
                };
                AppServices::instance().dialog_manager().show_file_dialog(
                    FileDialogType::Open,
                    // TRN Title of the file dialog that picks the certificate of a Connect Local server.
                    Biz::_u8L("Load the server or CA certificate"),
                    AppServices::instance().app_config().get<std::string>("last_used_directory"),
                    std::string{},
                    Wildcards::generate_wildcards(
                        Wildcards::TypeFlag::Certificate | Wildcards::TypeFlag::AllFiles
                    ),
                    callback
                );
            }
        );
    return {};
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::on_forget_certificate(const std::string&)
{
    ConnectLocalTrust::forget();
    // Reload the signed-out page: it checks the server again (and shows the certificate step if
    // the system does not trust it).
    m_certificate_checked_server.clear();
    return m_page.logged_out_page();
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::use_server(const std::string& normalized_url)
{
    m_setup_requested   = false;
    m_certificate_setup = false;
    stop_discovery();

    const Biz::Network::ServiceConfig& sc = Biz::Network::ServiceConfig::instance();
    const bool unchanged = sc.is_offline() && sc.offline_server_url() == normalized_url;

    // Saving runs the service mode switch (MainFrame), which reloads this tab against the new server.
    AppConfigInteractor& app_config_interactor = AppServices::instance().app_config_interactor();
    if (AppServices::instance().app_config().connect_local_url() != normalized_url) {
        app_config_interactor
            .set_item_value("connect_local_url", Domain::ConfigValue(normalized_url));
    }
    app_config_interactor.set_service_mode(Biz::Network::ServiceMode::Offline);

    // Nothing to switch, just leave the setup page.
    return unchanged ? m_page.home_page() : std::vector<BrowserLogicCommand>{};
}

// ---- Certificate of an https server ----

void ConnectLocalSetupLogic::probe_certificate(
    const std::string& normalized_url,
    ProbeKind kind,
    std::function<void(Biz::Network::TlsProbeResult)> done
)
{
    // A newer probe of the same kind replaces an older one. Foreground and background checks are
    // separate, so a background check never swallows the result the page waits for.
    Biz::Network::ServerTrustProbe& probe =
        kind == ProbeKind::Foreground ? m_foreground_probe : m_background_probe;
    SPDLOG_INFO("Connect Local: checking the certificate of {}", normalized_url);
    probe.start(
        normalized_url,
        ConnectLocalTrust::stored_trust_for(normalized_url),
        [url = normalized_url, done = std::move(done)](Biz::Network::TlsProbeResult result)
        {
            switch (result.status) {
            case Biz::Network::TlsProbeStatus::Ok:
                SPDLOG_INFO("Connect Local: certificate of {} accepted", url);
                break;
            case Biz::Network::TlsProbeStatus::CertificateRejected:
                SPDLOG_INFO("Connect Local: certificate of {} rejected: {}", url, result.error);
                break;
            case Biz::Network::TlsProbeStatus::Unreachable:
                SPDLOG_INFO("Connect Local: {} unreachable: {}", url, result.error);
                break;
            }
            done(std::move(result));
        }
    );
}

std::vector<BrowserLogicCommand>
ConnectLocalSetupLogic::check_and_use_server(const std::string& normalized_url)
{
    probe_certificate(
        normalized_url,
        ProbeKind::Foreground,
        [this, normalized_url](Biz::Network::TlsProbeResult result)
        { m_page.push_commands(finish_foreground_probe(normalized_url, result)); }
    );
    return {show_checking(true)};
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::
    finish_foreground_probe(const std::string& server, const Biz::Network::TlsProbeResult& result)
{
    switch (result.status) {
    case Biz::Network::TlsProbeStatus::CertificateRejected:
        return show_certificate_commands(server, result);
    case Biz::Network::TlsProbeStatus::Unreachable:
        return {
            show_checking(false),
            show_error(
                fmt::format(
                    // TRN {} is the technical reason, e.g. "Failed to connect to 192.168.1.10 port 8443: Connection refused".
                    fmt::runtime(Biz::_u8L("Cannot connect to the server: {}")),
                    result.error
                )
            )
        };
    case Biz::Network::TlsProbeStatus::Ok:
    default: {
        std::vector<BrowserLogicCommand> res = {show_checking(false)};
        std::vector<BrowserLogicCommand> use = use_server(server);
        res.insert(res.end(), use.begin(), use.end());
        return res;
    }
    }
}

std::vector<BrowserLogicCommand> ConnectLocalSetupLogic::show_certificate_commands(
    const std::string& normalized_url,
    const Biz::Network::TlsProbeResult& result
)
{
    if (!result.certificate) {
        return {show_checking(false)};
    }
    const Biz::Network::CertificateInfo& cert = *result.certificate;
    m_presented_certificate                   = cert;
    m_presented_certificate_url               = normalized_url;

    const Biz::Network::ServerTrust trust = ConnectLocalTrust::stored_trust_for(normalized_url);
    std::string error;
    if (trust.kind == Biz::Network::ServerTrustKind::PinnedKey) {
        error = Biz::_u8L(
            "The server presents a different certificate than the one you trusted. If the server got a new certificate, compare its fingerprint and trust it again."
        );
    } else if (trust.kind == Biz::Network::ServerTrustKind::Certificate) {
        error = fmt::format(
            fmt::runtime(
                // TRN {} is the technical reason, e.g. "SSL: certificate subject name does not match target host name".
                Biz::_u8L("The server certificate does not match the loaded certificate: {}")
            ),
            result.error
        );
    } else {
        error = fmt::format(
            fmt::runtime(
                // TRN {} is the technical reason, e.g. "SSL certificate problem: self-signed certificate".
                Biz::_u8L("Connection refused because the server certificate is not trusted: {}")
            ),
            result.error
        );
    }
    const nlohmann::json data = {
        {"url", normalized_url},
        {"fingerprint", cert.sha256_fingerprint},
        {"subject", cert.subject},
        {"names", cert.subject_alt_names},
        {"issuer", cert.issuer},
        {"selfSigned", cert.self_signed},
        {"notBefore", cert.not_before},
        {"notAfter", cert.not_after},
        {"expired", cert.expired},
        {"notYetValid", cert.not_yet_valid},
        {"error", error},
    };
    return {show_checking(false), call_setup_page("showCertificate", Biz::json_for_script(data))};
}

void ConnectLocalSetupLogic::check_current_server_certificate()
{
    const std::string server = Biz::Network::ServiceConfig::instance().offline_server_url();
    if (!is_https(server)) {
        return;
    }
    probe_certificate(
        server,
        ProbeKind::Background,
        [this, server](Biz::Network::TlsProbeResult result)
        {
            if (result.status == Biz::Network::TlsProbeStatus::CertificateRejected) {
                m_page.push_commands(show_certificate_commands(server, result));
            }
        }
    );
}

void ConnectLocalSetupLogic::check_certificate_before_sign_in()
{
    // Once per server, so that Cancel on the setup page can still come back here.
    const std::string server = Biz::Network::ServiceConfig::instance().offline_server_url();
    if (!is_https(server) || m_certificate_checked_server == server) {
        return;
    }
    m_certificate_checked_server = server;
    probe_certificate(
        server,
        ProbeKind::Background,
        [this](Biz::Network::TlsProbeResult result)
        {
            if (result.status == Biz::Network::TlsProbeStatus::CertificateRejected
                && m_local_page
                && !m_setup_requested
                && !m_user_account.is_logged_in())
            {
                m_setup_requested   = true;
                m_certificate_setup = true;
                m_page.push_commands(m_page.setup_page());
            }
        }
    );
}

} // namespace Slic3r::App::Browser
