#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Slic3r::App {
enum class AccountService;
} // namespace Slic3r::App

namespace Slic3r::App::Browser {

/**
 * @brief The switch between Prusa Connect and Prusa Connect on-premise at the top of the app's
 * own signed-out Connect tab pages (resources/web/connect_pages.js), shown while both services
 * are enabled. It makes the chosen service the active one; the Connect tab is then created
 * again for it. Only the app's own pages may switch, a page of a server cannot.
 */
class ConnectServiceSwitch
{
public:
    using Actions =
        std::map<std::string, std::function<std::vector<BrowserLogicCommand>(const std::string&)>>;

    static void add_texts(nlohmann::json& texts);

    /** @brief Whether the pages show the switch: both services are enabled. */
    static bool shown();

    void add_actions(Actions& actions);

    void on_page_loaded(const std::string& url);
    void on_navigation_request(const std::string& new_url);

    bool on_own_page() const
    {
        return m_own_page;
    }

private:
    std::vector<BrowserLogicCommand> choose(AccountService service) const;

    bool m_own_page{false};
};

} // namespace Slic3r::App::Browser
