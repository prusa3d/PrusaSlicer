#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Slic3r::Biz::Network {

/**
 * @brief The parts of an absolute URL that tell which server it connects to.
 *
 * Parsed by curl (CURLU), so they are what a request to the URL uses.
 */
struct UrlParts
{
    /** @brief Lower case. */
    std::string scheme;
    /** @brief Lower case. An IPv6 address without the brackets and without the zone id. */
    std::string host;
    /** @brief Zone id of an IPv6 link-local address (e.g. "en0"), empty when there is none. */
    std::string zone_id;
    /**
     * @brief The port of the URL, else the default port of the scheme (0 when the scheme has none).
     */
    int port{0};
    /** @brief Whether the URL spells out the port. */
    bool explicit_port{false};
    /** @brief As written (dot segments are kept), "/" when the URL has none. */
    std::string path;

    bool is_ipv6() const
    {
        return host.find(':') != std::string::npos;
    }

    /**
     * @brief "scheme://host[:port]": an IPv6 host in brackets, the port only when explicit.
     */
    std::string origin() const;
};

/**
 * @brief Splits an absolute URL ("scheme://host...") into its parts.
 *
 * Any scheme is accepted. Query, fragment and user info are allowed and not reported.
 * @return nullopt when curl rejects the URL, or when it has no host or port 0.
 */
std::optional<UrlParts> parse_url(std::string_view url);

/**
 * @brief Whether the URL is the server URL or a URL under it.
 *
 * Scheme, host and port are compared as parsed (case insensitive, default ports filled in). The
 * path of the URL must be the path of the server URL or continue it with a new segment
 * ("https://a/x" covers "https://a/x/y" and "https://a/x?q", not "https://a/xy"); paths are case
 * sensitive. Trailing slashes of the server URL are ignored.
 */
bool url_belongs_to_server(std::string_view url, std::string_view server_url);

/**
 * @brief Resolves a URL reference against an absolute base URL (RFC 3986, section 5), by curl.
 *
 * An absolute reference (one with a scheme) is returned as it is, a relative one is resolved
 * ("avatar.png?i=1" against "https://a/me/" is "https://a/me/avatar.png?i=1").
 * @return Empty when the reference is empty or cannot be resolved against the base.
 */
std::string resolve_url(std::string_view reference, std::string_view base_url);

} // namespace Slic3r::Biz::Network
