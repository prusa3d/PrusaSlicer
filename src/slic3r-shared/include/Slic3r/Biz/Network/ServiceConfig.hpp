#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace Slic3r::Biz::Network {

/**
 * @brief Which backend serves Prusa Account and Prusa Connect.
 *
 * Cloud is account.prusa3d.com and connect.prusa3d.com. ConnectLocal is a self-hosted Prusa
 * Connect on-premise server; every Account and Connect request goes there, with the same
 * request and response contracts, only base URLs and paths differ.
 */
enum class ServiceMode
{
    Cloud,
    ConnectLocal
};

/**
 * @brief All service URLs of the application.
 *
 * The Account and Connect URLs depend on the service mode, which can change at runtime (the
 * caller is responsible for logging out first, tokens belong to one server). Their getters are
 * thread safe and return copies; the other services do not depend on the mode.
 */
class ServiceConfig
{
    ServiceConfig();

public:

    /**
     * @brief Switches between the cloud and a Connect Local server.
     * @param connect_local_server_url Base URL of the Connect Local server, used in ConnectLocal mode only.
     * It is normalized by normalize_connect_local_server_url(); an invalid one leaves ConnectLocal mode
     * without a server (connect_local_server_url() empty, every Account/Connect URL empty).
     */
    void set_mode(ServiceMode mode, const std::string& connect_local_server_url = {});

    ServiceMode mode() const;

    bool is_connect_local() const
    {
        return mode() == ServiceMode::ConnectLocal;
    }

    /**
     * @brief Normalized base URL of the Connect Local server, empty in Cloud mode or when the
     * configured one is invalid.
     */
    std::string connect_local_server_url() const;

    /**
     * @brief Validates and normalizes the user entered Connect Local server URL.
     *
     * Not subject to the Prusa domain whitelist: http and https are allowed, with any host
     * (LAN IP, hostname, .local, localhost) and optional port and path prefix. A missing scheme
     * defaults to http. Trailing slashes are removed. Query, fragment, user info and
     * whitespace are rejected.
     * @return The normalized URL, or nullopt if the URL cannot be used.
     */
    static std::optional<std::string> normalize_connect_local_server_url(std::string_view url);

    /**
     * @brief Path under the Connect Local server where the PrusaSlicer module API is mounted.
     */
    static constexpr std::string_view CONNECT_LOCAL_MODULE_PATH =
        "/api/v1/modules/com.github.aux.prusa-research.prusaslicer3-compat";


    /**
     * @brief Base of the Connect HTTP API (uploads, printers, ...).
     */
    std::string connect_url() const;

    /**
     * @brief Page loaded in the Connect tab. Navigation away from this prefix is vetoed.
     */
    std::string connect_page_url() const;

    std::string connect_status_url() const
    {
        return connect_url() + "/slicer/status";
    }

    /**
     * @brief Base of the Connect slicer REST API (file records, printer queue). Empty Connect Local:
     * Connect Local serves the legacy API under connect_url() only.
     */
    std::string connect_api_url() const;

    /**
     * @brief GET here lists the user's Connect printers. The slicer REST API online, the legacy
     * endpoint of connect_url() Connect Local.
     */
    std::string connect_printer_list_url() const;

    /**
     * @brief POST here creates a file record in the organization and returns the presigned
     * upload URL. Empty Connect Local.
     */
    std::string connect_organization_files_url(const std::string& organization_id) const;

    /** @brief PATCH here finishes the upload of one file record. Empty Connect Local. */
    std::string connect_organization_file_url(
        const std::string& organization_id,
        const std::string& file_id
    ) const;

    /** @brief POST here sends an uploaded file to the printer or to its queue. Empty Connect Local. */
    std::string connect_printer_queue_url(const std::string& printer_uuid) const;

    /**
     * @brief The MQTT broker pushing printer states. Empty Connect Local: Connect Local has none, the
     * printer list is polled instead.
     */
    std::string connect_mqtt_url() const;

    std::string connect_select_printer_url() const;

    /**
     * @brief Page of one printer in the Connect web app, shown after an upload to it.
     * @param printer_uuid Uuid of the printer in the Connect printer list (Connect Local: the
     * device id).
     * @return Empty for an empty or unsafe uuid, or Connect Local without a valid server.
     */
    std::string connect_printer_page_url(const std::string& printer_uuid) const;

    std::string connect_printers_url() const
    {
        return connect_url() + "/app/printers/";
    }

    /**
     * @brief Printables "print to Connect" page of the cloud. Empty Connect Local, Connect Local has
     * none.
     */
    std::string connect_printables_print_url() const;

    /**
     * @brief Connect GraphQL endpoint (favorite materials) of the cloud. Empty Connect Local, Connect
     * Local has none.
     */
    std::string connect_graphql_url() const;


    /**
     * @brief Base of the Account API (token, me). Also the origin whose cookies are cleared after
     * login.
     */
    std::string account_url() const;

    const std::string& account_client_id() const
    {
        return m_account_client_id;
    }

    /**
     * @brief OAuth token endpoint. Empty Connect Local: Connect Local signs in by app pairing.
     */
    std::string account_token_url() const;

    /**
     * @brief Whether a Prusa Account can sign in (OAuth) with the current services. False Connect Local:
     * Connect Local signs in to the server by app pairing, not to Prusa Account or Printables.
     */
    bool prusa_account_available() const
    {
        return !account_token_url().empty();
    }

    std::string account_me_url() const
    {
        return account_url() + "/api/v1/me/";
    }

    std::string account_logout_url() const
    {
        return account_url() + "/logout";
    }


    /**
     * @brief Starts a pairing (POST, no auth).
     */
    std::string connect_local_pairing_url() const;

    /**
     * @brief Collects the credential of a pairing (POST, no auth).
     */
    std::string connect_local_pairing_token_url() const;

    /**
     * @brief Page where the person signs in and approves the pairing with the given user code.
     * Built from the server URL, not taken from the server reply. Empty for a malformed code.
     */
    std::string connect_local_pairing_page_url(const std::string& user_code) const;

    /**
     * @brief A credential (DELETE revokes it). Empty for a malformed id.
     */
    std::string connect_local_credential_url(const std::string& credential_id) const;

    /**
     * @brief Trades the credential (bearer) for a short page session cookie of the server's
     * web pages (POST), so that the pages never see the credential.
     */
    std::string connect_local_page_session_url() const;


    std::string printables_get_secret_token_url() const
    {
        return m_printables_url + "/auth/get-secret-token";
    }

    std::string media_url() const
    {
        return m_media_url + "/media/";
    }

    const std::string& preset_repo_url() const
    {
        return m_preset_repo_url;
    }

    void set_preset_repo_url(const std::string& url)
    {
        m_preset_repo_url = url;
    }

    std::string preset_repo_repos_url() const
    {
        return m_preset_repo_url + "/v2/repos";
    }

    bool webdev_enabled() const
    {
        return m_webdev_enabled;
    }

    void set_webdev_enabled(bool enabled)
    {
        m_webdev_enabled = enabled;
    }

    const std::string& printables_url() const
    {
        return m_printables_url;
    }

    const std::string& plugin_marketplace_url() const
    {
        return m_plugin_marketplace_url;
    }

    static ServiceConfig& instance();

private:
    /**
     * @brief The Account and Connect URLs of the current mode, computed by set_mode().
     */
    struct ModeUrls
    {
        ServiceMode mode{ServiceMode::Cloud};
        /** @brief Empty online and Connect Local without a valid server. */
        std::string connect_local_server;
        std::string connect;
        /** @brief Empty Connect Local, Connect Local has no slicer REST API. */
        std::string connect_api;
        std::string connect_printer_list;
        /** @brief Empty Connect Local, Connect Local has no MQTT broker and no GraphQL. */
        std::string connect_mqtt;
        std::string connect_graphql;
        std::string connect_printables_print;
        std::string connect_page;
        std::string connect_select_printer;
        std::string account;
        std::string account_token;
    };

    ModeUrls cloud_urls() const;
    static ModeUrls connect_local_urls(const std::string& server);

    /** @brief Reads one URL of the current mode. */
    std::string mode_url(std::string ModeUrls::* url) const;

    std::string m_connect_url;
    std::string m_connect_api_url;
    std::string m_connect_mqtt_url;
    std::string m_account_url;
    std::string m_account_client_id;
    std::string m_media_url;
    std::string m_preset_repo_url;
    std::string m_printables_url;
    std::string m_plugin_marketplace_url;
    bool m_webdev_enabled{false};

    mutable std::shared_mutex m_mutex;
    ModeUrls m_mode_urls;
};

} // namespace Slic3r::Biz::Network
