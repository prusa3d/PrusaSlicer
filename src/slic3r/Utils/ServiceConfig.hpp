#pragma once

#include <string>

namespace Slic3r::Utils {

class ServiceConfig {
    ServiceConfig();
public:
    const std::string& connect_url() const { return m_connect_url; }

    std::string connect_status_url() const { return m_connect_url + "/slicer/status"; }
    std::string connect_printer_list_url() const { return m_connect_url + "/slicer/printer_list"; }
    std::string connect_select_printer_url() const { return m_connect_url + "/slicer-select-printer"; }
    std::string connect_printers_url() const { return m_connect_url + "/app/printers/"; }
    std::string connect_teams_url() const { return m_connect_url + "/app/teams"; }
    std::string connect_printables_print_url() const { return m_connect_url + "/slicer-print"; }

    const std::string& account_url() const { return m_account_url; }
    const std::string& account_client_id() const { return m_account_client_id; }

    std::string account_token_url() const { return m_account_url + "/o/token/"; }
    std::string account_me_url() const { return m_account_url + "/api/v1/me/"; }
    std::string account_logout_url() const { return m_account_url + "/logout"; }

    std::string media_url() const { return m_media_url + "/media/"; }

    const std::string& preset_repo_url() const { return m_preset_repo_url; }
    std::string preset_repo_repos_url() const { return m_preset_repo_url + "/v1/repos"; }

    bool webdev_enabled() const { return m_webdev_enabled; }
    void set_webdev_enabled(bool enabled) { m_webdev_enabled = enabled; }

    const std::string& printables_url() const { return m_printables_url; }

    static ServiceConfig& instance();
private:
    std::string m_connect_url;
    std::string m_account_url;
    std::string m_account_client_id;
    std::string m_media_url;
    std::string m_preset_repo_url;
    std::string m_printables_url;
    bool m_webdev_enabled{false};
};

}
