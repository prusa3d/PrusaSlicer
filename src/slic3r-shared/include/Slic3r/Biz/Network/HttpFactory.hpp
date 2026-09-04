#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Assert.hpp"

#include <memory>

namespace Slic3r::Biz::Network {

/**
 * @brief This factory exists to support multiple IHttp implementations (in case libcurl is not available).
 */
class HttpFactory {
public:

    using CreateFn = std::unique_ptr<IHttp>(IHttp::RequestMethod, std::string, IHttp::RetryFn);
    using ExtractHostFn = std::string(const std::string&);
    using GetBaseUrlFn = std::string(const std::string&);
    using SubstituteHostFn = std::string(const std::string&, std::string);
    using EscapePathFn = std::string(const boost::filesystem::path&);
    using EscapeStringFn = std::string(const std::string&);
    using UnescapeStringFn = std::string(const std::string&);
    using IsSubdomainFn = bool(const std::string&, const std::string&);
    using GetApexDomainFn = std::string(const std::string&);
    using CaFileSupportedFn = bool();
    using TlsGlobalInitFn = std::string();
    using TlsSystemCertStoreFn = std::string();

    static HttpFactory& instance() {
        static HttpFactory instance; 
        return instance;
    }

    std::unique_ptr<IHttp> create(IHttp::RequestMethod request_method, std::string url, IHttp::RetryFn fn) {
        if(!m_create_fn) {
            initialize();
        }
        return m_create_fn(request_method, std::move(url), std::move(fn));
    }

    std::string extract_host_from_url(const std::string& url_in) {
        if(!m_extract_host_from_url_fn) {
            initialize();
        }
        return m_extract_host_from_url_fn(url_in);
    }

    std::string get_base_url(const std::string& url_in) {
        if (!m_get_base_url_fn) {
            initialize();
        }
        return m_get_base_url_fn(url_in);
    }

    std::string substitute_host(const std::string& orig_addr, std::string sub_addr) {
        if(!m_substitute_host_fn) {
            initialize();
        }
        return m_substitute_host_fn(orig_addr, std::move(sub_addr));
    }

    std::string escape_path_by_element(const boost::filesystem::path& path) {
        if(!m_escape_path_by_element_fn) {
            initialize();
        }
        return m_escape_path_by_element_fn(path);
    }

    std::string escape_string(const std::string& str) {
        if(!m_escape_string_fn) {
            initialize();
        }
        return m_escape_string_fn(str);
    }

    std::string unescape_string(const std::string& str) {
        if(!m_unescape_string_fn) {
            initialize();
        }
        return m_unescape_string_fn(str);
    }

    bool is_subdomain(const std::string& url, const std::string& domain) {
        if(!m_is_subdomain_fn) {
            initialize();
        }
        return m_is_subdomain_fn(url, domain);
    }

    std::string get_apex_domain(const std::string& url) {
        if (! m_get_apex_domain_fn) {
            initialize();
        }
        return m_get_apex_domain_fn(url);
    }


    bool ca_file_supported() {
        if(!m_ca_file_supported_fn) {
            initialize();
        }
        return m_ca_file_supported_fn();
    }

    std::string tls_global_init() {
        if(!m_tls_global_init_fn) {
            initialize();
        }
        return m_tls_global_init_fn();
    }

    std::string tls_system_cert_store() {
        if(!m_tls_system_cert_store_fn) {
            initialize();
        }
        return m_tls_system_cert_store_fn();
    }

    void set_create_fn(std::function<CreateFn> fn) {
        m_create_fn = std::move(fn);
    }

    void set_extract_host_from_url_fn(std::function<ExtractHostFn> fn) {
        m_extract_host_from_url_fn = std::move(fn);
    }

    void set_get_base_url_fn(std::function<GetBaseUrlFn> fn) {
        m_get_base_url_fn = std::move(fn);
    }

    void set_substitute_host_fn(std::function<SubstituteHostFn> fn) {
        m_substitute_host_fn = std::move(fn);
    }

    void set_escape_path_by_element_fn(std::function<EscapePathFn> fn) {
        m_escape_path_by_element_fn = std::move(fn);
    }

    void set_escape_string_fn(std::function<EscapeStringFn> fn) {
        m_escape_string_fn = std::move(fn);
    }

    void set_unescape_string_fn(std::function<UnescapeStringFn> fn) {
        m_unescape_string_fn = std::move(fn);
    }

    void set_is_subdomain_fn(std::function<IsSubdomainFn> fn) {
        m_is_subdomain_fn = std::move(fn);
    }

     void set_get_apex_domain_fn(std::function<GetApexDomainFn> fn) {
        m_get_apex_domain_fn = std::move(fn);
    }

    void set_ca_file_supported_fn(std::function<CaFileSupportedFn> fn) {
        m_ca_file_supported_fn = std::move(fn);
    }

    void set_tls_global_init_fn(std::function<TlsGlobalInitFn> fn) {
        m_tls_global_init_fn = std::move(fn);
    }

    void set_tls_system_cert_store_fn(std::function<TlsSystemCertStoreFn> fn) {
        m_tls_system_cert_store_fn = std::move(fn);
    }

private:
    HttpFactory() {
    }

    // Initialize is defined in f.e. HttpCurlFactory.cpp.
    // This way we can initialize with different implementations defined by CMAKE.
    // While maintaining extra implemetaions like MockHttp for test. (That sets functions before fist getter from HttpFactory)
    void initialize();    

    HttpFactory(const HttpFactory&) = delete;
    HttpFactory& operator=(const HttpFactory&) = delete;
    HttpFactory(HttpFactory&&) = delete;
    HttpFactory& operator=(HttpFactory&&) = delete;

    std::function<CreateFn> m_create_fn;
    std::function<ExtractHostFn> m_extract_host_from_url_fn;
    std::function<GetBaseUrlFn> m_get_base_url_fn;
    std::function<SubstituteHostFn> m_substitute_host_fn;
    std::function<EscapePathFn> m_escape_path_by_element_fn;
    std::function<EscapeStringFn> m_escape_string_fn;
    std::function<UnescapeStringFn> m_unescape_string_fn;
    std::function<IsSubdomainFn> m_is_subdomain_fn;
    std::function<GetApexDomainFn> m_get_apex_domain_fn;
    std::function<CaFileSupportedFn> m_ca_file_supported_fn;
    std::function<TlsGlobalInitFn> m_tls_global_init_fn;
    std::function<TlsSystemCertStoreFn> m_tls_system_cert_store_fn;
};

} // namespace Slic3r::Biz::Network::HttpFactory