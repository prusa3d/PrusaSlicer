#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Slic3r::App::Browser {

class BrowserLogicPhysicalPrinter final : public AbstractBrowserLogic
{
public:
    BrowserLogicPhysicalPrinter(const std::string& url,
                                const std::string& api_key,
                                const std::string& usr,
                                const std::string& psk);

    // AbstractBrowserLogic
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url) override;
    std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) override;
    std::vector<BrowserLogicCommand> on_load_default_url() override;
    std::vector<BrowserLogicCommand> on_show_webview_event(bool show) override;

    void update_connection(const std::string& url, const std::string& api_key, const std::string& usr, const std::string& psk);

private:
    std::map<std::string, std::function<std::vector<BrowserLogicCommand>(const std::string&)>> m_events;

    std::vector<BrowserLogicCommand> on_reload_event(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_dummy_event(const std::string& message_data) { return {}; }

    std::vector<BrowserLogicCommand> on_loading_html_loaded();
    std::vector<BrowserLogicCommand> on_base_url_loaded();
    std::vector<BrowserLogicCommand> on_target_url_loaded(const std::string& url);
    std::vector<BrowserLogicCommand> reconnect_commands();

    std::vector<BrowserLogicCommand> send_api_key();
    std::vector<BrowserLogicCommand> send_credentials();
    void emplace_define_css_commands(std::vector<BrowserLogicCommand>& res);

    std::string script_show_loading_overlay() const;
    std::string script_navigate_to_path(const std::string& path) const;

    bool uses_basic_auth() const { return m_api_key.empty() && !m_usr.empty() && !m_psk.empty(); }

    std::string m_api_key;
    std::string m_usr;
    std::string m_psk;

    std::string m_base_url;
    std::string m_last_target_url;
    bool m_reached_default_url{false};
    bool m_styles_defined{false};
    bool m_api_key_sent{false};
    bool m_load_default_url{true};
    bool m_base_url_preloaded{false};
    int m_auto_retry_count{0};
    bool m_connection_changed{false};
};

} // namespace Slic3r::App::Browser
