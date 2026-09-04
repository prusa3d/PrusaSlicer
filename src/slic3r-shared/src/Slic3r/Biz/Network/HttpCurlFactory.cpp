#include "Slic3r/Biz/Network/HttpFactory.hpp"
#include "Slic3r/Biz/Network/HttpCurl.hpp"

namespace Slic3r::Biz::Network {

// Lazy fallback used when a getter is queried for a slot that has not been
// configured yet. It must fill ONLY the unset slots and never overwrite an
// implementation that was installed explicitly (e.g. the test mock via
// configure_http_factory_with_mock).
void HttpFactory::initialize()
{
    if (!m_create_fn) {
        m_create_fn = [](IHttp::RequestMethod request_method, std::string url, IHttp::RetryFn fn) -> std::unique_ptr<IHttp> {
            return std::make_unique<HttpCurl>(request_method, std::move(url), std::move(fn));
        };
    }
    if (!m_extract_host_from_url_fn) {
        m_extract_host_from_url_fn = [](const std::string& url) -> std::string {
            return HttpCurl::extract_host_from_url(url);
        };
    }
    if (!m_substitute_host_fn) {
        m_substitute_host_fn = [](const std::string& orig_addr, std::string sub_addr) -> std::string {
            return HttpCurl::substitute_host(orig_addr, std::move(sub_addr));
        };
    }
    if (!m_escape_path_by_element_fn) {
        m_escape_path_by_element_fn = [](const boost::filesystem::path& path) -> std::string {
            return HttpCurl::escape_path_by_element(path);
        };
    }
    if (!m_escape_string_fn) {
        m_escape_string_fn = [](const std::string& str) -> std::string {
            return HttpCurl::escape_string(str);
        };
    }
    if (!m_unescape_string_fn) {
        m_unescape_string_fn = [](const std::string& str) -> std::string {
            return HttpCurl::unescape_string(str);
        };
    }
    if (!m_is_subdomain_fn) {
        m_is_subdomain_fn = [](const std::string& url, const std::string& domain) -> bool {
            return HttpCurl::is_subdomain(url, domain);
        };
    }
    if (!m_get_apex_domain_fn) {
        m_get_apex_domain_fn = [](const std::string& url) -> std::string {
            return HttpCurl::get_apex_domain(url);
        };
    }
    if (!m_ca_file_supported_fn) {
        m_ca_file_supported_fn = []() -> bool {
            return HttpCurl::ca_file_supported();
        };
    }
    if (!m_tls_global_init_fn) {
        m_tls_global_init_fn = []() -> std::string {
            return HttpCurl::tls_global_init();
        };
    }
    if (!m_tls_system_cert_store_fn) {
        m_tls_system_cert_store_fn = []() -> std::string {
            return HttpCurl::tls_system_cert_store();
        };
    }

    if (!m_get_base_url_fn) {
        m_get_base_url_fn = [](const std::string& url) -> std::string {
            return HttpCurl::get_base_url(url);
        };
    }
}
}