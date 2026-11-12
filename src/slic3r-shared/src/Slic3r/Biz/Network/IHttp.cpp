#include "Slic3r/Biz/Network/IHttp.hpp"

#include "Slic3r/Biz/Network/HttpFactory.hpp"
#include <sstream>

namespace Slic3r::Biz::Network {

const HttpRetryOpt& HttpRetryOpt::no_retry()
{
    using namespace std::chrono_literals;
    static HttpRetryOpt val = {0ms};
    return val;
}
const HttpRetryOpt& HttpRetryOpt::default_retry()
{
    using namespace std::chrono_literals;
    static HttpRetryOpt val = {500ms, std::chrono::milliseconds(MAX_RETRY_DELAY_MS), MAX_RETRIES};
    return val;
}

std::unique_ptr<IHttp> IHttp::create(RequestMethod request_method, std::string url, RetryFn fn)
{
    return HttpFactory::instance().create(request_method, std::move(url), std::move(fn));
}

std::string IHttp::extract_host_from_url(const std::string& url)
{
    return HttpFactory::instance().extract_host_from_url(url);
}
std::string IHttp::get_base_url(const std::string& url)
{
    return HttpFactory::instance().get_base_url(url);
}
std::string IHttp::substitute_host(const std::string& orig_addr, std::string sub_addr)
{
    return HttpFactory::instance().substitute_host(orig_addr, sub_addr);
}

std::string IHttp::escape_path_by_element(const boost::filesystem::path& path)
{
    return HttpFactory::instance().escape_path_by_element(path);
}

std::string IHttp::escape_string(const std::string& str)
{
    return HttpFactory::instance().escape_string(str);
}

std::string IHttp::unescape_string(const std::string& str)
{
    return HttpFactory::instance().unescape_string(str);
}

bool IHttp::is_subdomain(const std::string& url, const std::string& domain)
{
    return HttpFactory::instance().is_subdomain(url, domain);
}

std::string IHttp::get_apex_domain(const std::string& url)
{
    return HttpFactory::instance().get_apex_domain(url);
}


bool IHttp::ca_file_supported()
{
    return HttpFactory::instance().ca_file_supported();
}
std::string IHttp::tls_global_init()
{
    return HttpFactory::instance().tls_global_init();
}
std::string IHttp::tls_system_cert_store()
{
    return HttpFactory::instance().tls_system_cert_store();
}

IHttp& IHttp::on_complete(CompleteFn fn)
{
	completefn = std::move(fn);
	return *this;
}

IHttp& IHttp::on_error(ErrorFn fn)
{
	errorfn = std::move(fn);
	return *this;
}

IHttp& IHttp::on_progress(ProgressFn fn)
{
	progressfn = std::move(fn);
	return *this;
}

IHttp& IHttp::on_ip_resolve(IPResolveFn fn)
{
	ipresolvefn = std::move(fn);
	return *this;
}

IHttp& IHttp::on_headers_read(HeadersReadFn fn)
{
    headersfn = std::move(fn);
	return *this;
}


std::ostream& operator<<(std::ostream &os, const IHttp::Progress &progress)
{
	os << "Http::Progress("
		<< "dltotal = " << progress.dltotal
		<< ", dlnow = " << progress.dlnow
		<< ", ultotal = " << progress.ultotal
		<< ", ulnow = " << progress.ulnow
		<< ")";
	return os;
}

std::string IHttp::Progress::to_string() const
{
    std::ostringstream os;
	os << "Http::Progress("
		<< "dltotal = " << dltotal
		<< ", dlnow = " << dlnow
		<< ", ultotal = " << ultotal
		<< ", ulnow = " << ulnow
		<< ")";
	return os.str();
}

std::ostream& operator<<(std::ostream &os, const IHttp::Retry &retry)
{
	os << "Http::Progress("
		<< "attempt = " << retry.attempt
        << "("<< retry.just_tried << ")"
		<< ", ms_to_next_attempt = " << retry.ms_to_next_attempt
		<< ")";
	return os;
}

std::string IHttp::Retry::to_string() const
{
    std::ostringstream os;
    os << "Http::Retry("
        << "attempt = " << attempt
        << ", ms_to_next_attempt = " << ms_to_next_attempt
        << ")";
    return os.str();
}
} // namespace Slic3r::Biz::Network

