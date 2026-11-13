#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace Slic3r::Biz::Network {

/**
 * @brief Which backend serves Prusa Account and Prusa Connect.
 *
 * Online is the cloud (account.prusa3d.com, connect.prusa3d.com). Offline is a self-hosted
 * Prusa Connect Local server; every Account and Connect request goes there, with the same
 * request and response contracts, only base URLs and paths differ.
 */
enum class ServiceMode
{
    Online,
    Offline
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
    // ---- Service mode ----

    /**
     * @brief Switches between the cloud and a Connect Local server.
     * @param offline_server_url Base URL of the Connect Local server, used in Offline mode only.
     * It is normalized by normalize_offline_server_url(); an invalid one leaves Offline mode
     * without a server (offline_server_url() empty, every Account/Connect URL empty).
     */
    void set_mode(ServiceMode mode, const std::string& offline_server_url = {});

    ServiceMode mode() const;

    bool is_offline() const
    {
        return mode() == ServiceMode::Offline;
    }

    /**
     * @brief Normalized base URL of the Connect Local server, empty in Online mode or when the
     * configured one is invalid.
     */
    std::string offline_server_url() const;

    /**
     * @brief Validates and normalizes the user entered Connect Local server URL.
     *
     * Not subject to the Prusa domain whitelist: http and https are allowed, with any host
     * (LAN IP, hostname, .local, localhost) and optional port and path prefix. A missing scheme
     * defaults to http. Trailing slashes are removed. Query, fragment, user info and
     * whitespace are rejected.
     * @return The normalized URL, or nullopt if the URL cannot be used.
     */
    static std::optional<std::string> normalize_offline_server_url(std::string_view url);

    /**
     * @brief Path under the Connect Local server where the PrusaSlicer module API is mounted.
     */
    static constexpr std::string_view OFFLINE_MODULE_PATH =
        "/api/v1/modules/com.github.aux.prusa-research.prusaslicer3-compat";

    // ---- Prusa Connect ----

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

    const std::string& connect_api_url() const
    {
        return m_connect_api_url;
    }

    /** @brief GET here lists the user's Connect printers. */
    std::string connect_printer_list_url() const
    {
        return m_connect_api_url + "/printer/slicer/v1/printers";
    }

    /** @brief POST here creates a file record in the organization and returns the presigned upload URL. */
    std::string connect_organization_files_url(const std::string& organization_id) const
    {
        return m_connect_api_url + "/file/slicer/v1/organizations/" + organization_id + "/files";
    }

    /** @brief PATCH here finishes the upload of one file record. */
    std::string connect_organization_file_url(
        const std::string& organization_id,
        const std::string& file_id
    ) const
    {
        return connect_organization_files_url(organization_id) + "/" + file_id;
    }

    /** @brief POST here sends an uploaded file to the printer or to its queue. */
    std::string connect_printer_queue_url(const std::string& printer_uuid) const
    {
        return m_connect_api_url + "/printer/slicer/v1/printers/" + printer_uuid + "/queue";
    }

    const std::string& connect_mqtt_url() const
    {
        return m_connect_mqtt_url;
    }

    std::string connect_select_printer_url() const;

    std::string connect_printers_url() const
    {
        return connect_url() + "/app/printers/";
    }

    /**
     * @brief Printables "print to Connect" page of the cloud. Connect Local has none.
     */
    std::string connect_printables_print_url() const
    {
        return m_connect_url + "/slicer-print";
    }

    /**
     * @brief Connect GraphQL endpoint (favorite materials) of the cloud. Connect Local has none.
     */
    std::string connect_graphql_url() const
    {
        return m_connect_api_url + "/graphql";
    }

    // ---- Prusa Account ----

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
     * @brief OAuth token endpoint. Empty offline: Connect Local signs in by app pairing.
     */
    std::string account_token_url() const;

    std::string account_me_url() const
    {
        return account_url() + "/api/v1/me/";
    }

    std::string account_logout_url() const
    {
        return account_url() + "/logout";
    }

    // ---- Other services (not affected by the mode) ----

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
        ServiceMode mode{ServiceMode::Online};
        /** @brief Empty online and offline without a valid server. */
        std::string offline_server;
        std::string connect;
        std::string connect_page;
        std::string connect_select_printer;
        std::string account;
        std::string account_token;
    };

    ModeUrls online_urls() const;
    static ModeUrls offline_urls(const std::string& server);

    /** @brief Reads one URL of the current mode. */
    std::string mode_url(std::string ModeUrls::* url) const;

    // Online URLs, not affected by the mode.
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
