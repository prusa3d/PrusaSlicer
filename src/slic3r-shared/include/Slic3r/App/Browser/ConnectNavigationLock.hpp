#pragma once

#include <string>

namespace Slic3r::App::Browser {

/**
 * @brief Keeps the Connect tab on its service once the Connect page was reached.
 *
 * Until the tab navigates to the Connect page itself, every navigation passes: the tab is still
 * showing the app's loading or signed-out pages. From then on only pages of the service pass,
 * and, where the tab says so, the app's own resource pages.
 */
class ConnectNavigationLock
{
public:
    /**
     * @param connect_page_url The Connect page of the service, empty when there is none.
     * @param loading_page Name of the app's loading page, which never passes a locked tab.
     */
    ConnectNavigationLock(std::string connect_page_url, std::string loading_page);

    /** @brief Whether the URL is the Connect page or a page of the same server under it. */
    bool is_connect_url(const std::string& url) const;

    /**
     * @brief Decides one navigation and remembers that the Connect page was reached.
     * @param app_pages_pass The app's own resource pages (file: URLs under /web/) pass a locked
     * tab too.
     * @return False when the navigation has to be vetoed.
     */
    bool allows(const std::string& new_url, bool app_pages_pass);

    bool locked() const
    {
        return m_locked;
    }

private:
    std::string m_connect_page_url;
    std::string m_loading_page;
    bool m_locked{false};
};

} // namespace Slic3r::App::Browser
