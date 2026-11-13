#include "Slic3r/Biz/Network/Url.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <curl/curl.h>
#include <fmt/format.h>

#include <charconv>
#include <locale>
#include <memory>

namespace Slic3r::Biz::Network {

namespace {

struct CurlUrlDeleter
{
    void operator()(CURLU* url) const
    {
        curl_url_cleanup(url);
    }
};

using CurlUrlPtr = std::unique_ptr<CURLU, CurlUrlDeleter>;

std::optional<std::string> get_part(CURLU* url, CURLUPart part, unsigned int flags = 0)
{
    char* value = nullptr;
    if (curl_url_get(url, part, &value, flags) != CURLUE_OK || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    curl_free(value);
    return result;
}

std::string to_lower(const std::string& value)
{
    return boost::algorithm::to_lower_copy(value, std::locale::classic());
}

int to_port(const std::string& value)
{
    int port                = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    return error == std::errc() && end == value.data() + value.size() ? port : 0;
}

} // namespace

std::string UrlParts::origin() const
{
    std::string result = scheme + "://";
    if (is_ipv6()) {
        // The zone id is percent encoded in a URL (RFC 6874).
        result += "[" + host + (zone_id.empty() ? std::string() : "%25" + zone_id) + "]";
    } else {
        result += host;
    }
    if (explicit_port) {
        result += fmt::format(":{}", port);
    }
    return result;
}

std::optional<UrlParts> parse_url(std::string_view url)
{
    const CurlUrlPtr handle(curl_url());
    const std::string text(url);
    // Any scheme, the path as written.
    const unsigned int flags = CURLU_NON_SUPPORT_SCHEME | CURLU_PATH_AS_IS;
    if (!handle || curl_url_set(handle.get(), CURLUPART_URL, text.c_str(), flags) != CURLUE_OK) {
        return std::nullopt;
    }
    const std::optional<std::string> scheme = get_part(handle.get(), CURLUPART_SCHEME);
    std::optional<std::string> host         = get_part(handle.get(), CURLUPART_HOST);
    if (!scheme || !host || host->empty()) {
        return std::nullopt;
    }
    if (host->size() > 2 && host->front() == '[' && host->back() == ']') {
        *host = host->substr(1, host->size() - 2);
    }

    UrlParts parts;
    parts.scheme  = to_lower(*scheme);
    parts.host    = to_lower(*host);
    parts.zone_id = get_part(handle.get(), CURLUPART_ZONEID).value_or(std::string());
    parts.path    = get_part(handle.get(), CURLUPART_PATH).value_or("/");

    parts.explicit_port = get_part(handle.get(), CURLUPART_PORT).has_value();
    const std::optional<std::string> port =
        get_part(handle.get(), CURLUPART_PORT, CURLU_DEFAULT_PORT);
    parts.port = port ? to_port(*port) : 0;
    if (parts.explicit_port && parts.port == 0) {
        return std::nullopt;
    }
    return parts;
}

bool url_belongs_to_server(std::string_view url, std::string_view server_url)
{
    const std::optional<UrlParts> server = parse_url(server_url);
    const std::optional<UrlParts> parts  = parse_url(url);
    if (!server
        || !parts
        || parts->scheme != server->scheme
        || parts->host != server->host
        || parts->zone_id != server->zone_id
        || parts->port != server->port)
    {
        return false;
    }
    std::string_view prefix = server->path;
    while (prefix.ends_with('/')) {
        prefix.remove_suffix(1);
    }
    const std::string_view path = parts->path;
    return path.starts_with(prefix) && (path.size() == prefix.size() || path[prefix.size()] == '/');
}

std::string resolve_url(std::string_view reference, std::string_view base_url)
{
    if (reference.empty()) {
        return {};
    }
    const std::string text(reference);
    const unsigned int flags = CURLU_NON_SUPPORT_SCHEME;

    const CurlUrlPtr absolute(curl_url());
    if (absolute && curl_url_set(absolute.get(), CURLUPART_URL, text.c_str(), flags) == CURLUE_OK) {
        return text;
    }

    // Setting a relative URL on a handle holding an absolute one resolves it against that one.
    const CurlUrlPtr handle(curl_url());
    const std::string base(base_url);
    if (!handle
        || curl_url_set(handle.get(), CURLUPART_URL, base.c_str(), flags) != CURLUE_OK
        || curl_url_set(handle.get(), CURLUPART_URL, text.c_str(), flags) != CURLUE_OK)
    {
        return {};
    }
    return get_part(handle.get(), CURLUPART_URL).value_or(std::string());
}

} // namespace Slic3r::Biz::Network
