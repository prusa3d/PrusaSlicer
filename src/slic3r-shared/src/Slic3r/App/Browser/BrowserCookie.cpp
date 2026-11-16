#include "Slic3r/App/Browser/BrowserCookie.hpp"

#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {

namespace {
/** @brief The fields of a page session reply and of a SetCookie command have the same names. */
std::optional<BrowserCookie> cookie_from_json(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return std::nullopt;
    }
    BrowserCookie cookie;
    cookie.url       = j.value("url", std::string());
    cookie.name      = j.value("name", std::string());
    cookie.value     = j.value("value", std::string());
    cookie.path      = j.value("path", std::string("/"));
    cookie.http_only = j.value("http_only", true);
    cookie.secure    = j.value("secure", false);
    cookie.same_site = j.value("same_site", std::string("Strict"));
    if (cookie.name.empty() || cookie.value.empty()) {
        return std::nullopt;
    }
    return cookie;
}
} // namespace

std::string cookie_set_command_data(const BrowserCookie& cookie)
{
    const nlohmann::json data{
        {"url", cookie.url},
        {"name", cookie.name},
        {"value", cookie.value},
        {"path", cookie.path},
        {"http_only", cookie.http_only},
        {"secure", cookie.secure},
        {"same_site", cookie.same_site},
    };
    return data.dump();
}

std::optional<BrowserCookie> parse_cookie_set_command_data(const std::string& data)
{
    try {
        return cookie_from_json(nlohmann::json::parse(data));
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<BrowserCookie>
parse_page_session(const std::string& body, const std::string& server_url)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        if (!j.is_object() || !j.contains("cookie")) {
            return std::nullopt;
        }
        std::optional<BrowserCookie> cookie = cookie_from_json(j["cookie"]);
        if (!cookie) {
            return std::nullopt;
        }
        if (cookie->name != CONNECT_LOCAL_SESSION_COOKIE) {
            SPDLOG_ERROR("Connect Local page session: unexpected cookie name {}.", cookie->name);
            return std::nullopt;
        }
        cookie->url = server_url;
        return cookie;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace Slic3r::App::Browser
