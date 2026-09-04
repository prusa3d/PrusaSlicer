#include "Slic3r/Biz/Network/HttpCurl.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Platform.hpp"

#include "Slic3r/Version.hpp"

#include "fmt/format.h"
#include <boost/nowide/fstream.hpp>
#include <random>
#include <thread>

#ifdef OPENSSL_CERT_OVERRIDE
#include <openssl/x509.h>
#endif

namespace fs = boost::filesystem;

namespace Slic3r::Biz::Network {

struct CurlGlobalInit
{
    static std::unique_ptr<CurlGlobalInit> instance;
    std::string message;

    CurlGlobalInit()
    {
#ifdef OPENSSL_CERT_OVERRIDE // defined if SLIC3R_STATIC=ON

        // Look for a set of distro specific directories. Don't change the
        // order: https://bugzilla.redhat.com/show_bug.cgi?id=1053882
        static const char* CA_BUNDLES[] = {
            "/etc/pki/tls/certs/ca-bundle.crt", // Fedora/RHEL 6
            "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Gentoo etc.
            "/usr/share/ssl/certs/ca-bundle.crt",
            "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD
            "/etc/ssl/cert.pem",
            "/etc/ssl/ca-bundle.pem" // OpenSUSE Tumbleweed
        };

        namespace fs = boost::filesystem;
        // Env var name for the OpenSSL CA bundle (SSL_CERT_FILE nomally)
        const char* const SSL_CA_FILE = X509_get_default_cert_file_env();
        const char* ssl_cafile        = ::getenv(SSL_CA_FILE);

        if (!ssl_cafile)
            ssl_cafile = X509_get_default_cert_file();

        int replace = true;
        if (!ssl_cafile || !fs::exists(fs::path(ssl_cafile))) {
            const char* bundle = nullptr;
            for (const char* b : CA_BUNDLES) {
                if (fs::exists(fs::path(b))) {
                    ::setenv(SSL_CA_FILE, bundle = b, replace);
                    break;
                }
            }

            if (!bundle)
                message = _u8L(
                    "Could not detect system SSL certificate store. "
                    "PrusaSlicer will be unable to establish secure "
                    "network connections."
                );
            else
                message = fmt::format(
                    "{} {}",
                    _u8L("PrusaSlicer detected system SSL certificate store in:"),
                    bundle
                );

            message += "\n"
                + _u8L("To specify the system certificate store manually, please set the ")
                + SSL_CA_FILE
                + _u8L(" environment variable to the correct CA bundle and restart the application.");
        }

#endif // OPENSSL_CERT_OVERRIDE

        if (CURLcode ec = ::curl_global_init(CURL_GLOBAL_DEFAULT)) {
            message +=
                "CURL init has failed. PrusaSlicer will be unable to establish "
                "network connections. See logs for additional details.";

            SPDLOG_ERROR("{}",::curl_easy_strerror(ec));
        }
    }

    ~CurlGlobalInit()
    {
        ::curl_global_cleanup();
    }
};

std::unique_ptr<CurlGlobalInit> CurlGlobalInit::instance;

constexpr int DEFAULT_TIMEOUT_CONNECT = 10;
constexpr int DEFAULT_TIMEOUT_MAX     = 0;
constexpr int DEFAULT_SIZE_LIMIT      = 5 * 1'024 * 1'024;

HttpCurl::HttpCurl(RequestMethod request_method, std::string&& url, RetryFn fn) :
    IHttp(request_method, std::move(url), std::move(fn)),
    m_curl(curl_easy_init()),
    m_error_buffer(CURL_ERROR_SIZE + 1, '\0')
{
    HttpCurl::tls_global_init();

    if (!m_curl) {
        throw Slic3r::RuntimeError(std::string("Could not construct Curl object"));
    }

    std::string user_agent = fmt::format("%1%/%2% (%3%)",SLIC3R_APP_NAME, SLIC3R_VERSION, platform_to_string(platform()));

    timeout_connection(DEFAULT_TIMEOUT_CONNECT);
    timeout_total(DEFAULT_TIMEOUT_MAX);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_URL, m_url.c_str()); // curl makes a copy internally
    ::curl_easy_setopt(m_curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    ::curl_easy_setopt(m_curl.get(), CURLOPT_ERRORBUFFER, &m_error_buffer.front());
    ::curl_easy_setopt(m_curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    switch (m_request_method) {
    case IHttp::RequestMethod::Post:
        curl_easy_setopt(m_curl.get(), CURLOPT_POST, 1L);
        break;
    case IHttp::RequestMethod::Put:
        curl_easy_setopt(m_curl.get(), CURLOPT_UPLOAD, 1L);
        break;
    case IHttp::RequestMethod::Get:
    default:
        break;
    }
}

HttpCurl::~HttpCurl() 
{
}

bool HttpCurl::ca_file_supported_inner(::CURL* curl)
{
#if defined(_WIN32) || defined(__APPLE__)
    bool res = false;
#else
    bool res = true;
#endif

    if (curl == nullptr) {
        return res;
    }

#if LIBCURL_VERSION_NUM >= 0x073000 // equivalent to v7.48 or greater
    ::curl_tlssessioninfo* tls;
    if (::curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &tls) == CURLE_OK) {
        if (tls->backend == CURLSSLBACKEND_SCHANNEL || tls->backend == CURLSSLBACKEND_DARWINSSL) {
            // With Windows and OS X native SSL support, cert files cannot be set
            // DK: OSX is now not building CURL and links system one, thus we do not know which backend is installed. Still, false will be returned since the ifdef at the begining if this function.
            res = false;
        }
    }
#endif

    return res;
}

size_t HttpCurl::writecb(void* data, size_t size, size_t nmemb, void* userp)
{
    auto self             = static_cast<HttpCurl*>(userp);
    const char* cdata     = static_cast<char*>(data);
    const size_t realsize = size * nmemb;
    const size_t limit    = self->m_limit > 0 ? self->m_limit : DEFAULT_SIZE_LIMIT;
    if (self->m_buffer.size() + realsize > limit) {
        // This makes curl_easy_perform return CURLE_WRITE_ERROR
        return 0;
    }

    self->m_buffer.append(cdata, realsize);

    return realsize;
}

size_t HttpCurl::headercb(void *data, size_t size, size_t nmemb, void *userp)
{
    std::string header(reinterpret_cast<char*>(data), size * nmemb);
    std::string *header_data = static_cast<std::string*>(userp);
    header_data->append(header);
    return size * nmemb;
}


int HttpCurl::xfercb(void* userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto self      = static_cast<HttpCurl*>(userp);
    bool cb_cancel = false;

    if (self->progressfn) {
        Progress progress(dltotal, dlnow, ultotal, ulnow, self->m_buffer);
        self->progressfn(progress, cb_cancel);
    }

    if (cb_cancel) {
        self->m_cancel = true;
    }

    return self->m_cancel;
}

int HttpCurl::xfercb_legacy(void* userp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    return xfercb(userp, dltotal, dlnow, ultotal, ulnow);
}

size_t HttpCurl::form_file_read_cb(char* buffer, size_t size, size_t nitems, void* userp)
{
    auto stream = reinterpret_cast<fs::ifstream*>(userp);

    try {
        stream->read(buffer, size * nitems);
    } catch (const std::exception&) {
        return CURL_READFUNC_ABORT;
    }

    return stream->gcount();
}

size_t HttpCurl::form_memory_read_cb(char* buffer, size_t size, size_t nitems, void* userp)
{
    std::string* uploadData = static_cast<std::string*>(userp);
    size_t bytesToCopy      = size * nitems;
    if (uploadData->empty())
        return 0;
    size_t copySize = std::min(bytesToCopy, uploadData->size());
    std::memcpy(buffer, uploadData->c_str(), copySize);
    uploadData->erase(0, copySize); // Remove copied data
    return copySize;
}

void HttpCurl::form_add_file_inner(const char* name, const fs::path& path, const char* filename)
{
    // We can't use CURLFORM_FILECONTENT, because curl doesn't support Unicode filenames on Windows
    // and so we use CURLFORM_STREAM with boost ifstream to read the file.

    if (filename == nullptr) {
        filename = path.string().c_str();
    }

    m_form_files.emplace_back(path, std::ios::in | std::ios::binary);
    auto& stream = m_form_files.back();
    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();
    stream.seekg(0);

    if (filename != nullptr) {
        curl_httppost* raw_form     = m_form.release();
        ::curl_formadd(
            &raw_form,
            &m_form_end,
            CURLFORM_COPYNAME,
            name,
            CURLFORM_FILENAME,
            filename,
            CURLFORM_CONTENTTYPE,
            "application/octet-stream",
            CURLFORM_STREAM,
            static_cast<void*>(&stream),
            CURLFORM_CONTENTSLENGTH,
            static_cast<long>(size),
            CURLFORM_END
        );
        m_form.reset(raw_form);
    }
}

std::string HttpCurl::curl_error(CURLcode curlcode)
{
    return fmt::format(
        "{}:\n{}\n[Error {}]",
        ::curl_easy_strerror(curlcode),
        m_error_buffer.c_str(),
        std::to_string(curlcode)
    );
}

std::string HttpCurl::body_size_error()
{
    return fmt::format("HTTP body data size exceeded m_limit ({} bytes)", m_limit);
}

namespace {
bool is_transient_error(CURLcode res, long http_status)
{
    if (res == CURLE_OK || res == CURLE_HTTP_RETURNED_ERROR)
        return http_status == 408 || http_status >= 500;
    return res == CURLE_COULDNT_CONNECT
        || res == CURLE_COULDNT_RESOLVE_HOST
        || res == CURLE_OPERATION_TIMEDOUT;
}
} // namespace

void HttpCurl::perform_sync(const HttpRetryOpt& retry_opts)
{
    using namespace std::chrono_literals;
    static thread_local std::mt19937 generator;
    std::uniform_int_distribution<std::chrono::milliseconds::rep> randomized_delay(
        retry_opts.initial_delay.count(),
        (retry_opts.initial_delay.count() * 3) / 2
    );

    ::curl_easy_setopt(m_curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_WRITEFUNCTION, writecb);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_WRITEDATA, static_cast<void*>(this));
    ::curl_easy_setopt(m_curl.get(), CURLOPT_READFUNCTION, form_file_read_cb);

    ::curl_easy_setopt(m_curl.get(), CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_NUM >= 0x072000 // equivalent to v7.32 or higher
    ::curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFOFUNCTION, xfercb);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFODATA, static_cast<void*>(this));
#ifndef _WIN32
    (void) xfercb_legacy; // prevent unused function warning
#endif
#else
    ::curl_easy_setopt(m_curl.get(), CURLOPT_PROGRESSFUNCTION, xfercb);
    ::curl_easy_setopt(m_curl.get(), CURLOPT_PROGRESSDATA, static_cast<void*>(this));
#endif

    ::curl_easy_setopt(m_curl.get(), CURLOPT_VERBOSE, spdlog::get_level() == spdlog::level::trace);

    std::string header_data;
    curl_easy_setopt(m_curl.get(), CURLOPT_HEADERFUNCTION, headercb);
    curl_easy_setopt(m_curl.get(), CURLOPT_HEADERDATA, &header_data);

    if (m_header_list) {
        ::curl_easy_setopt(m_curl.get(), CURLOPT_HTTPHEADER, m_header_list.get());
    }

    if (m_form) {
        ::curl_easy_setopt(m_curl.get(), CURLOPT_HTTPPOST, m_form.get());
    }

    if (!m_post_fields.empty()) {
        ::curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDS, m_post_fields.c_str());
        ::curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, m_post_fields.size());
    }

    if (!m_mime_data.empty()) {
        ASSERT(m_upload_data.empty(), "Both data for Post and Put operations are present.");
        curl_mime* mime     = curl_mime_init(m_curl.get());
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filename(part, m_mime_path.string().c_str());
        curl_mime_data(part, m_mime_data.c_str(), m_mime_data.size());
        curl_easy_setopt(m_curl.get(), CURLOPT_MIMEPOST, mime);
    }

    if (!m_upload_data.empty()) {
        ASSERT(m_mime_data.empty(), "Both data for Post and Put operations are present.");
        curl_easy_setopt(m_curl.get(), CURLOPT_READDATA, &m_upload_data);
        curl_easy_setopt(m_curl.get(), CURLOPT_INFILESIZE_LARGE, (curl_off_t) m_upload_data.size());

        // Function to provide data to CURL
        curl_easy_setopt(m_curl.get(), CURLOPT_READFUNCTION, form_memory_read_cb);
    }

    bool retry;
    CURLcode res;
    long http_status                = 0;
    std::chrono::milliseconds delay = std::chrono::milliseconds(randomized_delay(generator));
    size_t num_retries              = 0;

    do {
        ASSERT(
            retryfn,
            "retryfn needs to be set for every transaction - otherwise if transaction falls into transient error loop and app closes, thread with HttpCurl would hold app closing indefinetelly."
        );
        // break if canceled outside
        bool retry_fn_cancel = false;
        retryfn(
            {num_retries + 1, num_retries < retry_opts.max_retries ? (unsigned) delay.count() : 0, true},
            retry_fn_cancel
        );

        if (retry_fn_cancel) {
            res      = CURLE_ABORTED_BY_CALLBACK;
            m_cancel = true;
            break;
        }

        res = ::curl_easy_perform(m_curl.get());
        // Clear used data.
        m_upload_data.clear();
        m_mime_data.clear();
        m_upload_path.clear();
        m_mime_path.clear();

        if (res == CURLE_OK) {
            ::curl_easy_getinfo(m_curl.get(), CURLINFO_RESPONSE_CODE, &http_status);
        }

        retry = retry_opts.initial_delay > 0ms && is_transient_error(res, http_status);
        if (retry && retry_opts.max_retries > 0 && num_retries >= retry_opts.max_retries) {
            retry = false;
        }

        if (retry) {
            num_retries++;
            SPDLOG_ERROR("HTTP Transient error (code={}, http_status={}), retrying in {}s", std::to_string(res), std::to_string(http_status), std::to_string(delay.count() / 1000.0f));

            auto start_time = std::chrono::steady_clock::now();
            auto end_time   = start_time + delay;
            while (std::chrono::steady_clock::now() < end_time) {
                bool retry_fn_cancel    = false;
                unsigned remaining_time = duration_cast<std::chrono::milliseconds>(
                                              end_time - std::chrono::steady_clock::now()
                )
                                              .count();
                retryfn({num_retries, remaining_time, false}, retry_fn_cancel);
                if (retry_fn_cancel) {
                    res      = CURLE_ABORTED_BY_CALLBACK;
                    m_cancel = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            delay = std::min(delay * 2, retry_opts.max_delay);
        }

    } while (retry);

    m_put_file.reset();

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            if (m_cancel) {
                // The abort comes from the request being cancelled programmatically
                Progress dummy_progress(0, 0, 0, 0, std::string());
                bool cancel = true;
                if (progressfn) {
                    progressfn(dummy_progress, cancel);
                }
            } else {
                // The abort comes from the CURLOPT_READFUNCTION callback, which means reading file failed
                if (errorfn) {
                    errorfn(std::move(m_buffer), "Error reading file for file upload", 0);
                }
            }
        } else if (res == CURLE_WRITE_ERROR) {
            if (errorfn) {
                errorfn(std::move(m_buffer), body_size_error(), 0);
            }
        } else {
            if (errorfn) {
                errorfn(std::move(m_buffer), curl_error(res), 0);
            }
        };
    } else {
        if (http_status >= 400) {
            if (errorfn) {
                errorfn(std::move(m_buffer), std::string(), http_status);
            }
        } else {
            if (headersfn && !header_data.empty()) {
                headersfn(header_data);
            }
            if (completefn) {
                completefn(std::move(m_buffer), http_status);
            }
            if (ipresolvefn) {
                char* ct;
                res = curl_easy_getinfo(m_curl.get(), CURLINFO_PRIMARY_IP, &ct);
                if ((CURLE_OK == res) && ct) {
                    ipresolvefn(ct);
                }
            }
        }
    }
}

IHttp& HttpCurl::timeout_connection(long timeout)
{
    if (timeout < 1) {
        timeout = DEFAULT_TIMEOUT_CONNECT;
    }
    ::curl_easy_setopt(m_curl.get(), CURLOPT_CONNECTTIMEOUT, timeout);
    return *this;
}

IHttp& HttpCurl::timeout_total(long timeout)
{
    if (timeout < 1) {
        timeout = DEFAULT_TIMEOUT_MAX;
    }
    ::curl_easy_setopt(m_curl.get(), CURLOPT_TIMEOUT, timeout);
    return *this;
}

IHttp& HttpCurl::size_limit(size_t sizeLimit)
{
    m_limit = sizeLimit;
    return *this;
}

IHttp& HttpCurl::set_range(const std::string& range)
{
    ::curl_easy_setopt(m_curl.get(), CURLOPT_RANGE, range.c_str());
    return *this;
}

IHttp& HttpCurl::header(std::string name, const std::string& value)
{
    if (name.size() > 0) {
        name.append(": ").append(value);
    } else {
        name.push_back(':');
    }
    m_header_list.reset(curl_slist_append(m_header_list.release(), name.c_str()));
    return *this;
}

IHttp& HttpCurl::remove_header(std::string name)
{
    name.push_back(':');
    m_header_list.reset(curl_slist_append(m_header_list.release(), name.c_str()));
    return *this;
}

IHttp& HttpCurl::auth_digest(const std::string& user, const std::string& password)
{
    // Authorization by HTTP digest, based on RFC2617.
    curl_easy_setopt(m_curl.get(), CURLOPT_USERNAME, user.c_str());
    curl_easy_setopt(m_curl.get(), CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(m_curl.get(), CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

    return *this;
}

IHttp& HttpCurl::auth_basic(const std::string& user, const std::string& password)
{
    curl_easy_setopt(m_curl.get(), CURLOPT_USERNAME, user.c_str());
    curl_easy_setopt(m_curl.get(), CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(m_curl.get(), CURLOPT_HTTPAUTH, CURLAUTH_BASIC);

    return *this;
}

IHttp& HttpCurl::ca_file(const std::string& name)
{
    if (ca_file_supported_inner(m_curl.get())) {
        ::curl_easy_setopt(m_curl.get(), CURLOPT_CAINFO, name.c_str());
    }

    return *this;
}

IHttp& HttpCurl::form_add(const std::string& name, const std::string& contents)
{
    curl_httppost* raw_form     = m_form.release();
    ::curl_formadd(
        &raw_form,
        &m_form_end,
        CURLFORM_COPYNAME,
        name.c_str(),
        CURLFORM_COPYCONTENTS,
        contents.c_str(),
        CURLFORM_END
    );

    m_form.reset(raw_form);

    return *this;
}

IHttp& HttpCurl::form_add_file(const std::string& name, const fs::path& path)
{
    form_add_file_inner(name.c_str(), path.c_str(), nullptr);
    return *this;
}

IHttp& HttpCurl::form_add_file(const std::string& name, const fs::path& path, const std::string& filename)
{
    form_add_file_inner(name.c_str(), path.c_str(), filename.c_str());
    return *this;
}

IHttp& HttpCurl::form_add_data(const std::string& name, const std::string&& data, const fs::path& upload_path)
{
    m_mime_data = std::move(data);
    m_mime_path = upload_path;

    return *this;
}

IHttp& HttpCurl::set_put_data(std::string&& data, const boost::filesystem::path& upload_path)
{
    m_upload_data = std::move(data);
    m_upload_path = upload_path;

    return *this;
}

#ifdef WIN32
// Tells libcurl to ignore certificate revocation checks in case of missing or offline distribution points for those SSL backends where such behavior is present.
// This option is only supported for Schannel (the native Windows SSL library).
IHttp& HttpCurl::ssl_revoke_best_effort(bool set)
{
    if (set) {
        ::curl_easy_setopt(m_curl.get(), CURLOPT_SSL_OPTIONS, CURLSSLOPT_REVOKE_BEST_EFFORT);
    }
    return *this;
}
#endif // WIN32

IHttp& HttpCurl::set_post_body(const fs::path& path)
{
    boost::system::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        SPDLOG_ERROR("Failed to set POST body. File {} does not exists. {}", path.string(), ec.message());
        return *this;
    }
    boost::nowide::ifstream file(path.string());
    std::string file_content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    m_post_fields = std::move(file_content);
    return *this;
}

IHttp& HttpCurl::set_post_body(std::string body)
{
    m_post_fields = std::move(body);
    return *this;
}

IHttp& HttpCurl::set_referer(const std::string& referer)
{
    ::curl_easy_setopt(m_curl.get(), CURLOPT_REFERER, referer.c_str());
    return *this;
}

IHttp& HttpCurl::set_put_body(const fs::path& path)
{
    boost::system::error_code ec;
    boost::uintmax_t file_size = fs::file_size(path, ec);
    if (!ec) {
        m_put_file = std::make_unique<fs::ifstream>(path, std::ios::binary);
        ::curl_easy_setopt(m_curl.get(), CURLOPT_READDATA, (void*) (m_put_file.get()));
        ::curl_easy_setopt(m_curl.get(), CURLOPT_INFILESIZE, file_size);
    } else {
        SPDLOG_ERROR("Failed to set PUT body with file {}. {}", path.string(), ec.message());
    }
    return *this;
}

IHttp& HttpCurl::cookie_file(const std::string& file_path)
{
    ::curl_easy_setopt(m_curl.get(), CURLOPT_COOKIEFILE, file_path.c_str());
    return *this;
}

IHttp& HttpCurl::cookie_jar(const std::string& file_path)
{
    ::curl_easy_setopt(m_curl.get(), CURLOPT_COOKIEJAR, file_path.c_str());
    return *this;
}

std::string HttpCurl::extract_host_from_url(const std::string& url_in)
{
    std::string url = url_in;

    // Ensure scheme exists (default to http://)
    if (url.find("://") == std::string::npos) {
        url = "http://" + url;
    }
    CURLU* curlu = curl_url();
    if (!curlu) {
        SPDLOG_ERROR("extract_host_from_url: Failed to allocate CURLU handle");
        return url_in;
    }

    std::string host;
    if (curl_url_set(curlu, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
        SPDLOG_ERROR("extract_host_from_url: Failed to parse URL: {}", url);
        curl_url_cleanup(curlu);
        return url_in;
    }
    char* host_cstr = nullptr;
    if (curl_url_get(curlu, CURLUPART_HOST, &host_cstr, 0) != CURLUE_OK) {
        SPDLOG_ERROR("extract_host_from_url: Failed to extract host from URL: ", url);
        curl_url_cleanup(curlu);
        return url_in;
    }
    host = host_cstr;
    curl_free(host_cstr);
    curl_url_cleanup(curlu);
    return host;
}

std::string HttpCurl::get_base_url(const std::string& url_in)
{
    std::string url = url_in;
    if (url.find("://") == std::string::npos) {
        url = "http://" + url;
    }

    CURLU* curlu = curl_url();
    if (!curlu) {
        SPDLOG_ERROR("get_base_url: Failed to allocate CURLU handle");
        return url_in;
    }

    if (curl_url_set(curlu, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
        SPDLOG_ERROR("get_base_url: Failed to parse URL: {}", url);
        curl_url_cleanup(curlu);
        return url_in;
    }

    char* scheme_cstr = nullptr;
    if (curl_url_get(curlu, CURLUPART_SCHEME, &scheme_cstr, 0) != CURLUE_OK) {
        SPDLOG_ERROR("get_base_url: Failed to extract scheme from URL: {}", url);
        curl_url_cleanup(curlu);
        return url_in;
    }

    char* host_cstr = nullptr;
    if (curl_url_get(curlu, CURLUPART_HOST, &host_cstr, 0) != CURLUE_OK) {
        curl_free(scheme_cstr);
        SPDLOG_ERROR("get_base_url: Failed to extract host from URL: {}", url);
        curl_url_cleanup(curlu);
        return url_in;
    }

    std::string base_url = std::string(scheme_cstr) + "://" + std::string(host_cstr);
    curl_free(scheme_cstr);
    curl_free(host_cstr);

    // Include port only if explicitly present in the URL
    char* port_cstr = nullptr;
    if (curl_url_get(curlu, CURLUPART_PORT, &port_cstr, 0) == CURLUE_OK) {
        base_url += ":" + std::string(port_cstr);
        curl_free(port_cstr);
    }

    curl_url_cleanup(curlu);
    return base_url;
}

std::string HttpCurl::substitute_host(const std::string& orig_addr, std::string sub_addr)
{
    // Ensure IPv6 addresses are enclosed in brackets
    if (sub_addr.find(':') != std::string::npos && sub_addr.front() != '[') {
        sub_addr.insert(0, "[");
        sub_addr.push_back(']');
    }

    CURLU* curlu = curl_url();
    if (!curlu) {
        SPDLOG_ERROR("substitute_host: Failed to allocate CURLU handle");
        return orig_addr;
    }

    std::string result = orig_addr;

    // Parse the input URL
    if (curl_url_set(curlu, CURLUPART_URL, orig_addr.c_str(), 0) != CURLUE_OK) {
        SPDLOG_ERROR("substitute_host: Failed to parse URL: {}", orig_addr);
        curl_url_cleanup(curlu);
        return orig_addr;
    }

    // Replace the host
    if (curl_url_set(curlu, CURLUPART_HOST, sub_addr.c_str(), 0) != CURLUE_OK) {
        SPDLOG_ERROR("substitute_host: Failed to substitute host: {} in URL: {}", sub_addr, orig_addr);
        curl_url_cleanup(curlu);
        return orig_addr;
    }

    // Extract modified URL
    char* new_url = nullptr;
    if (curl_url_get(curlu, CURLUPART_URL, &new_url, 0) == CURLUE_OK) {
        result = new_url;
        curl_free(new_url);
    } else {
        SPDLOG_ERROR("substitute_host: Failed to extract modified URL");
    }

    curl_url_cleanup(curlu);
    return result;
}

namespace {
std::string escape_string_inner(const std::string& unescaped, CURL* curl)
{
    if (curl == nullptr) {
        return unescaped;
    }
    char* ce            = ::curl_easy_escape(curl, unescaped.c_str(), unescaped.length());
    std::string encoded = std::string(ce);
    ::curl_free(ce);
    return encoded;
}
} // namespace

std::string HttpCurl::escape_path_by_element(const boost::filesystem::path& path)
{
    CURL* curl          = curl_easy_init();
    std::string ret_val = escape_string_inner(path.filename().string(), curl);
    boost::filesystem::path parent(path.parent_path());
    while (
        !parent.empty() && parent.string() != "/"
    ) // "/" check is for case "/file.gcode" was inserted. Then boost takes "/" as parent_path.
    {
        ret_val = escape_string_inner(parent.filename().string(), curl) + "/" + ret_val;
        parent  = parent.parent_path();
    }
    curl_easy_cleanup(curl);
    return ret_val;
}

std::string HttpCurl::escape_string(const std::string& str)
{
    CURL* curl          = curl_easy_init();
    std::string ret_val = escape_string_inner(str, curl);
    curl_easy_cleanup(curl);
    return ret_val;
}

std::string HttpCurl::unescape_string(const std::string& str)
{
    std::string ret_val;
    int decodelen;
    char* decoded = curl_easy_unescape(nullptr, str.c_str(), str.size(), &decodelen);
    if (decoded) {
        ret_val = std::string(decoded);
        curl_free(decoded);
    }
    return ret_val;
}

bool HttpCurl::is_subdomain(const std::string& url, const std::string& domain)
{
    // domain should be f.e. printables.com (.com including)
	char* host;
	std::string host_string;
	CURLUcode rc;
	CURLU* curl = curl_url();
	if (!curl) {
		SPDLOG_ERROR("Failed to init Curl library in function is_domain.");
		return false;
	}
	rc = curl_url_set(curl, CURLUPART_URL, url.c_str(), 0);
	if (rc != CURLUE_OK) {
		curl_url_cleanup(curl);
		return false;
	}
	rc = curl_url_get(curl, CURLUPART_HOST, &host, 0);
	if (rc != CURLUE_OK || !host) {
		curl_url_cleanup(curl);
		return false;
	}
	host_string = std::string(host);
	curl_free(host);
	// now host should be subdomain.domain or just domain
	if (domain == host_string) {
		curl_url_cleanup(curl);
		return true;
	}
    const std::string suffix = "." + domain;
	if(host_string.ends_with(suffix)) {
		curl_url_cleanup(curl);
		return true;
	}
	curl_url_cleanup(curl);
	return false;
}

std::string HttpCurl::get_apex_domain(const std::string& url) 
{
    CURLU* h = curl_url();
    if (!h) return {};

    if (curl_url_set(h, CURLUPART_URL, url.c_str(), CURLU_DEFAULT_SCHEME) != CURLUE_OK) {
        curl_url_cleanup(h);
        return {};
    }

    char* host = nullptr;
    if (curl_url_get(h, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
        curl_url_cleanup(h);
        return {};
    }

    std::string host_str(host);
    curl_free(host);
    curl_url_cleanup(h);

    size_t last_dot = host_str.rfind('.');
    if (last_dot != std::string::npos && last_dot > 0) {
        size_t prev_dot = host_str.rfind('.', last_dot - 1);
        if (prev_dot != std::string::npos) {
            return host_str.substr(prev_dot + 1);
        }
    }
    return host_str;
}

bool HttpCurl::ca_file_supported()
{
    ::CURL* curl = ::curl_easy_init();
    bool res     = ca_file_supported_inner(curl);
    if (curl != nullptr) {
        ::curl_easy_cleanup(curl);
    }
    return res;
}

std::string HttpCurl::tls_global_init()
{
    if (!CurlGlobalInit::instance)
        CurlGlobalInit::instance = std::make_unique<CurlGlobalInit>();

    return CurlGlobalInit::instance->message;
}

std::string HttpCurl::tls_system_cert_store()
{
    std::string ret;

#ifdef OPENSSL_CERT_OVERRIDE
    ret = ::getenv(X509_get_default_cert_file_env());
#endif

    return ret;
}

} // namespace Slic3r::Biz::Network
