#include "Slic3r/App/Browser/ConnectNavigationLock.hpp"

#include "Slic3r/Biz/Network/Url.hpp"

#include <utility>

namespace Slic3r::App::Browser {

ConnectNavigationLock::
    ConnectNavigationLock(std::string connect_page_url, std::string loading_page) :
    m_connect_page_url(std::move(connect_page_url)),
    m_loading_page(std::move(loading_page))
{}

bool ConnectNavigationLock::is_connect_url(const std::string& url) const
{
    return !m_connect_page_url.empty()
        && Biz::Network::url_belongs_to_server(url, m_connect_page_url);
}

bool ConnectNavigationLock::allows(const std::string& new_url, bool app_pages_pass)
{
    if (!m_connect_page_url.empty() && new_url == m_connect_page_url) {
        m_locked = true;
        return true;
    }
    if (!m_locked || new_url.find("connection_failed.html") != std::string::npos) {
        return true;
    }
    const bool app_page = new_url.starts_with("file:")
        && new_url.find("/web/") != std::string::npos;
    const bool loading_page = new_url.find("/web/" + m_loading_page) != std::string::npos;
    if (app_page && app_pages_pass && !loading_page) {
        return true;
    }
    return is_connect_url(new_url);
}

} // namespace Slic3r::App::Browser
