#include "MockHttpFactory.hpp"

#include "Slic3r/Biz/Network/HttpFactory.hpp"
#include "MockHttp.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz::Network {

void configure_http_factory_with_mock() {
    auto& factory = HttpFactory::instance();

    factory.set_create_fn([](IHttp::RequestMethod request_method, std::string url, IHttp::RetryFn fn) -> std::unique_ptr<IHttp> {
        ASSERT(MockHttpProvider::get().create_fn());
        return MockHttpProvider::get().create_fn()(request_method, url, fn);
    });

    // All static functions must be set, otherwise default factory will run configure again and reconfigure Http implementation.

    factory.set_extract_host_from_url_fn([](const std::string& url) -> std::string {
        return "mock.host";
    });

    factory.set_substitute_host_fn([](const std::string& orig_addr, std::string sub_addr) -> std::string {
        return orig_addr;
    });

    factory.set_escape_path_by_element_fn([](const boost::filesystem::path& path) -> std::string {
        return path.string();
    });

    factory.set_escape_string_fn([](const std::string& str) -> std::string {
         return str;
    });

    factory.set_unescape_string_fn([](const std::string& str) -> std::string {
        return str;
    });

    factory.set_is_subdomain_fn([](const std::string& url, const std::string& domain) -> bool {
        return false;
    });

    factory.set_get_apex_domain_fn([](const std::string& url) -> std::string {
        return "mock.host";
    });

    factory.set_ca_file_supported_fn([]() -> bool {
        return true;
    });

    factory.set_tls_global_init_fn([]() -> std::string {
        return "";
    });

    factory.set_tls_system_cert_store_fn([]() -> std::string {
        return "";
    });
}
}