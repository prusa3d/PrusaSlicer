#pragma once

#include <string>

namespace Slic3r::Biz::Network {

class ServiceConfig
{
    ServiceConfig();

public:
    const std::string& connect_url() const
    {
        return m_connect_url;
    }

    std::string connect_status_url() const
    {
        return m_connect_url + "/slicer/status";
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

    std::string connect_select_printer_url() const
    {
        return m_connect_url + "/slicer-select-printer";
    }

    std::string connect_printers_url() const
    {
        return m_connect_url + "/app/printers/";
    }

    std::string connect_printables_print_url() const
    {
        return m_connect_url + "/slicer-print";
    }

    std::string connect_graphql_url() const
    {
        return m_connect_api_url + "/graphql";
    }

    const std::string& account_url() const
    {
        return m_account_url;
    }

    const std::string& account_client_id() const
    {
        return m_account_client_id;
    }

    std::string account_token_url() const
    {
        return m_account_url + "/o/token/";
    }

    std::string printables_get_secret_token_url() const
    {
        return m_printables_url + "/auth/get-secret-token";
    }

    std::string account_me_url() const
    {
        return m_account_url + "/api/v1/me/";
    }

    std::string account_logout_url() const
    {
        return m_account_url + "/logout";
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
};

} // namespace Slic3r::Biz::Network
