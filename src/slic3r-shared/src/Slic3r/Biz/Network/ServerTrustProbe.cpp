#include "Slic3r/Biz/Network/ServerTrustProbe.hpp"

#include "Slic3r/Biz/Network/HttpCurl.hpp"
#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <curl/curl.h>
#include <fmt/format.h>

#include <cstring>
#include <memory>
#include <utility>

namespace Slic3r::Biz::Network {

namespace {

using CurlPtr = std::unique_ptr<CURL, HttpCurl::CurlDeleter>;

constexpr long PROBE_CONNECT_TIMEOUT = 5;
constexpr long PROBE_TOTAL_TIMEOUT   = 10;

int on_progress(void* cancelled, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto& is_cancelled = *static_cast<const std::function<bool()>*>(cancelled);
    return is_cancelled && is_cancelled() ? 1 : 0;
}

CurlPtr connect_only_handle(
    const std::string& url,
    std::string& error_buffer,
    const std::function<bool()>& cancelled
)
{
    HttpCurl::tls_global_init();
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        return curl;
    }
    error_buffer.assign(CURL_ERROR_SIZE + 1, '\0');
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, PROBE_CONNECT_TIMEOUT);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, PROBE_TOTAL_TIMEOUT);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &on_progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancelled);
    return curl;
}

std::string describe(CURLcode code, const std::string& error_buffer)
{
    const char* detail = error_buffer.c_str();
    return std::strlen(detail) > 0 ? std::string(detail) : std::string(curl_easy_strerror(code));
}

bool is_connection_failure(CURLcode code)
{
    return code == CURLE_COULDNT_RESOLVE_HOST
        || code == CURLE_COULDNT_RESOLVE_PROXY
        || code == CURLE_COULDNT_CONNECT
        || code == CURLE_OPERATION_TIMEDOUT
        || code == CURLE_URL_MALFORMAT
        || code == CURLE_UNSUPPORTED_PROTOCOL
        || code == CURLE_ABORTED_BY_CALLBACK;
}

/**
 * @brief The inspection handshake: verification off so the handshake completes and curl hands over
 * the certificate (CURLOPT_CERTINFO). Never used for anything but reading that certificate: no
 * credentials, no request, the connection is closed when the handle is cleaned up.
 */
std::optional<CertificateInfo>
fetch_presented_certificate(const std::string& url, const std::function<bool()>& cancelled)
{
    std::string error_buffer;
    CurlPtr curl = connect_only_handle(url, error_buffer, cancelled);
    if (!curl) {
        return std::nullopt;
    }
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_CERTINFO, 1L);
    const CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        SPDLOG_WARN("Could not read the certificate of {}: {}", url, describe(res, error_buffer));
        return std::nullopt;
    }
    curl_certinfo* certinfo = nullptr;
    if (curl_easy_getinfo(curl.get(), CURLINFO_CERTINFO, &certinfo) != CURLE_OK
        || certinfo == nullptr
        || certinfo->num_of_certs < 1)
    {
        return std::nullopt;
    }
    for (curl_slist* field = certinfo->certinfo[0]; field != nullptr; field = field->next) {
        constexpr std::string_view CERT_FIELD = "Cert:";
        if (field->data != nullptr
            && std::strncmp(field->data, CERT_FIELD.data(), CERT_FIELD.size()) == 0)
        {
            return Tls::certificate_info(field->data + CERT_FIELD.size());
        }
    }
    return std::nullopt;
}

} // namespace

TlsProbeResult probe_server_certificate(
    const std::string& server_url,
    const ServerTrust& trust,
    const std::function<bool()>& cancelled
)
{
    TlsProbeResult result;
    const std::optional<UrlParts> url = parse_url(server_url);
    if (!url) {
        result.status = TlsProbeStatus::Unreachable;
        result.error  = "Invalid URL";
        return result;
    }
    if (url->scheme != "https") {
        result.status = TlsProbeStatus::Ok;
        return result;
    }

    std::string error_buffer;
    CurlPtr curl = connect_only_handle(server_url, error_buffer, cancelled);
    if (!curl) {
        result.error = "Could not construct Curl object";
        return result;
    }
    if (trust.is_custom()) {
        HttpCurl::apply_server_trust(curl.get(), trust);
    }
    const CURLcode res = curl_easy_perform(curl.get());
    curl.reset();
    if (res == CURLE_OK) {
        result.status = TlsProbeStatus::Ok;
        return result;
    }

    result.error = describe(res, error_buffer);
    if (is_connection_failure(res)) {
        result.status = TlsProbeStatus::Unreachable;
        return result;
    }
    result.certificate = fetch_presented_certificate(server_url, cancelled);
    result.status =
        result.certificate ? TlsProbeStatus::CertificateRejected : TlsProbeStatus::Unreachable;
    SPDLOG_INFO("Connect Local server {} certificate check failed: {}", server_url, result.error);
    return result;
}

ServerTrustProbe::ServerTrustProbe(std::string job_name) : m_job_name(std::move(job_name)) {}

ServerTrustProbe::~ServerTrustProbe()
{
    cancel();
}

void ServerTrustProbe::start(const std::string& server_url, const ServerTrust& trust, DoneFn done)
{
    cancel();
    static unsigned job_count = 0;
    const unsigned generation = m_generation;
    m_running_job             = fmt::format("{} #{}", m_job_name, ++job_count);
    Biz::Lifetime::Token alive = m_lifetime.token();

    using StopToken = JThread::StopToken;
    Platform::PlatformServices::instance()
        .job_manager()
        .create_job(
            m_running_job,
            [server_url, trust](StopToken stop)
            {
                return probe_server_certificate(
                    server_url,
                    trust,
                    [stop]() { return stop.stop_requested(); }
                );
            }
        )
        .on_result(
            [this, alive, generation, done](TlsProbeResult result)
            {
                if (alive.expired() || generation != m_generation) {
                    return;
                }
                m_running_job.clear();
                done(std::move(result));
            }
        )
        .on_exception(
            [this, alive, generation, done, server_url](const std::exception_ptr& exception)
            {
                TlsProbeResult result;
                try {
                    std::rethrow_exception(exception);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Certificate check of {} failed: {}", server_url, e.what());
                    result.error = e.what();
                } catch (...) {
                    SPDLOG_ERROR("Certificate check of {} failed.", server_url);
                    result.error = "Unknown error";
                }
                if (alive.expired() || generation != m_generation) {
                    return;
                }
                m_running_job.clear();
                done(std::move(result));
            }
        )
        .start();
}

void ServerTrustProbe::cancel()
{
    ++m_generation;
    Platform::PlatformServices& services = Platform::PlatformServices::instance();
    if (!m_running_job.empty() && services.has_job_manager()) {
        services.job_manager().request_job_stop(m_running_job);
    }
    m_running_job.clear();
}

} // namespace Slic3r::Biz::Network
