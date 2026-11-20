#pragma once

#include <optional>
#include <string>

namespace Slic3r::App::Browser {

/**
 * @brief Name of the session cookie of a Connect Local server, set by its own pages (sign-in) and
 * by the app (page sessions).
 */
constexpr char CONNECT_LOCAL_SESSION_COOKIE[] = "pcl_session";

/**
 * @brief A cookie the logic puts into the web views' cookie store
 * (BrowserLogicCommandType::SetCookie), host-only and without an expiry: the server ends the
 * session.
 */
struct BrowserCookie
{
    /** @brief The server the cookie belongs to (its scheme, host and port count). */
    std::string url;
    std::string name;
    std::string value;
    std::string path{"/"};
    bool http_only{true};
    bool secure{false};
    /** @brief "Strict", "Lax" or "None". */
    std::string same_site{"Strict"};
};

std::string cookie_set_command_data(const BrowserCookie& cookie);

/** @brief nullopt when the name or the value is missing. */
std::optional<BrowserCookie> parse_cookie_set_command_data(const std::string& data);

/**
 * @brief The cookie of a Connect Local page session reply ({"cookie": {...}}) for the server at
 * server_url, nullopt when it is incomplete or not named CONNECT_LOCAL_SESSION_COOKIE (the name
 * logging out deletes).
 */
std::optional<BrowserCookie>
parse_page_session(const std::string& body, const std::string& server_url);

} // namespace Slic3r::App::Browser
