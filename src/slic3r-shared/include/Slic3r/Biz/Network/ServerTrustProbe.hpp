#pragma once

#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/Tls.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r::Biz::Network {

/**
 * @brief Outcome of a certificate check of a server.
 */
enum class TlsProbeStatus
{
    /** @brief The certificate is accepted (or the server is not https). */
    Ok,
    /** @brief TLS works, but the certificate is not accepted by the trust. */
    CertificateRejected,
    /** @brief No TLS connection at all (host unknown, refused, timeout, ...). */
    Unreachable,
};

/**
 * @brief Result of a certificate check of a server, see probe_server_certificate().
 */
struct TlsProbeResult
{
    TlsProbeStatus status{TlsProbeStatus::Unreachable};
    /** @brief curl's description of the failure. */
    std::string error;
    /** @brief CertificateRejected: the certificate the server presented. */
    std::optional<CertificateInfo> certificate;
};

/**
 * @brief Checks the certificate of a Connect Local server. Blocking, run it off the main thread.
 *
 * Only the TCP connection and the TLS handshake are made (CURLOPT_CONNECT_ONLY), no HTTP request
 * is sent. The handshake is first verified with the given trust. Only when that fails, a second
 * handshake without verification fetches the presented certificate for the user to inspect;
 * nothing is sent over that connection and it is closed right away. Each handshake ends within
 * curl's timeouts (10 s).
 * @param cancelled Polled while connecting (about once a second); true aborts the check, the
 * result is then Unreachable.
 */
TlsProbeResult probe_server_certificate(
    const std::string& server_url,
    const ServerTrust& trust,
    const std::function<bool()>& cancelled = {}
);

/**
 * @brief Runs probe_server_certificate() off the main thread, one check at a time.
 *
 * Main thread only. The check runs as a job of the JobManager (Platform::PlatformServices), which
 * waits for it at shutdown; a cancelled check aborts within about a second. The result is
 * delivered on the main thread, never after cancel(), a newer start() or destruction.
 */
class ServerTrustProbe
{
public:
    using DoneFn = std::function<void(TlsProbeResult)>;

    /**
     * @param job_name Name of the jobs in the JobManager (a number is added to each).
     */
    explicit ServerTrustProbe(std::string job_name);
    ~ServerTrustProbe();

    ServerTrustProbe(const ServerTrustProbe&)            = delete;
    ServerTrustProbe& operator=(const ServerTrustProbe&) = delete;

    /**
     * @brief Checks the certificate of the server, cancels a check still running.
     */
    void start(const std::string& server_url, const ServerTrust& trust, DoneFn done);

    /**
     * @brief Aborts the running check, its result is dropped. Does not wait.
     */
    void cancel();

private:
    std::string m_job_name;
    /** @brief Job of the running check, empty when none runs. */
    std::string m_running_job;
    unsigned m_generation{0};
    /** @brief Expires with this object: results arriving later are dropped. */
    std::shared_ptr<char> m_alive{std::make_shared<char>(0)};
};

} // namespace Slic3r::Biz::Network
