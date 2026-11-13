#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Network/ServerTrust.hpp"
#include "Slic3r/Biz/Network/ServerTrustProbe.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/Tls.hpp"

#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Slic3r::Biz;
using Network::ServerTrust;
using Network::ServerTrustKind;
using Network::ServiceConfig;
namespace Tls               = Network::Tls;

namespace {

// Test certificates (EC P-256, valid until 2126), made with openssl:
// SELF: self-signed, "O=Prusa Test, CN=connect-local.test", SAN DNS:connect-local.test, IP:192.168.1.10.
// CA: "CN=Test Local CA". SRV: "CN=localhost", SAN DNS:localhost, IP:127.0.0.1, issued by CA.
// OTHER_CA: "CN=Other CA", unrelated.
const std::string SELF_PEM = R"(-----BEGIN CERTIFICATE-----
MIIBvjCCAWWgAwIBAgIUFeFRNzUee68GSTGA0aq7iOAxLQ8wCgYIKoZIzj0EAwIw
MjETMBEGA1UECgwKUHJ1c2EgVGVzdDEbMBkGA1UEAwwSY29ubmVjdC1sb2NhbC50
ZXN0MCAXDTI2MTExMjE2NTY1NloYDzIxMjYxMDE5MTY1NjU2WjAyMRMwEQYDVQQK
DApQcnVzYSBUZXN0MRswGQYDVQQDDBJjb25uZWN0LWxvY2FsLnRlc3QwWTATBgcq
hkjOPQIBBggqhkjOPQMBBwNCAASTTgjD0wxq2t541IvaQR6/LqCET0SdMIYJ3Ga4
RTCw+05iMJUazYnxAoAdk2XDHlRPUrLMd2L/+lrw3xP00DdXo1cwVTAdBgNVHQ4E
FgQUwZm+41ybPNLqeCIWvWR2R6HYRuUwDwYDVR0TAQH/BAUwAwEB/zAjBgNVHREE
HDAaghJjb25uZWN0LWxvY2FsLnRlc3SHBMCoAQowCgYIKoZIzj0EAwIDRwAwRAIg
F5yXXWwfrGbAptRaOQDslILavgdj5wBV4UYnopvrkoMCIF8tanQTEydfz9E5k2iH
k6wAAMIUGUA1hSmIjeiuihtL
-----END CERTIFICATE-----
)";
// openssl x509 -pubkey -noout | openssl pkey -pubin -outform der | openssl dgst -sha256 -binary | base64
const std::string SELF_PIN = "sha256//z7+w9avsWPbWzK64E9JMkro0fWzH8FNF/1FAK7psMDA=";
// openssl x509 -noout -fingerprint -sha256
const std::string SELF_FINGERPRINT =
    "C1:C1:41:DA:A4:6B:29:F3:B8:76:F5:64:DB:C1:D3:85:F1:83:3E:98:FB:3C:38:AC:C4:C5:17:B1:B4:2D:09:E4";

const std::string CA_PEM = R"(-----BEGIN CERTIFICATE-----
MIIBdzCCARygAwIBAgIULg8vUOMgQT/bmwzs/db2MNpyI6owCgYIKoZIzj0EAwIw
GDEWMBQGA1UEAwwNVGVzdCBMb2NhbCBDQTAgFw0yNjExMTIxNjU2NTZaGA8yMTI2
MTAxOTE2NTY1NlowGDEWMBQGA1UEAwwNVGVzdCBMb2NhbCBDQTBZMBMGByqGSM49
AgEGCCqGSM49AwEHA0IABJhg11wSMoF0+Bhn+MiW3q7ZD4tPmiMaN/3iDy0SHSio
ncUVCPYqaR3/iUD0uWd/el9iK+SsYTtSIlU0zmf/C92jQjBAMB0GA1UdDgQWBBRH
we+6S/5SaBHtIjgDK/qKRAQtiDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQE
AwIBBjAKBggqhkjOPQQDAgNJADBGAiEAqILEo81J9EuB1qBZkHaBlDgfFSIWUDB6
NnxhsYqyLi4CIQDIuCSasMJAxlU+kLik5yS11jje5TnjcC9zXuduC0eNWw==
-----END CERTIFICATE-----
)";

const std::string OTHER_CA_PEM = R"(-----BEGIN CERTIFICATE-----
MIIBbDCCARKgAwIBAgIUCQsDBuoq824MH0ZnWyukNNuabYowCgYIKoZIzj0EAwIw
EzERMA8GA1UEAwwIT3RoZXIgQ0EwIBcNMjYxMTEyMTY1NjU2WhgPMjEyNjEwMTkx
NjU2NTZaMBMxETAPBgNVBAMMCE90aGVyIENBMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAEnF5TUor4/WFUKCSiShSFd7Aty3DAvi0jJhX2SmOAdSskoq1KdkWYrw3L
jpJjCCvJicERXl9SdUYD8uyq5xwBy6NCMEAwHQYDVR0OBBYEFIIR3AHIMFT3ATBE
SJ2NP3hRdHQnMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMAoGCCqG
SM49BAMCA0gAMEUCIQDt+BdzWE6+20ZcW5CKkSrxQKhILOqzFPudug5JdRjvWAIg
IEeNLDB4t6X3YL2wlXknOu3goe5tlnvwjKtDqb8+Efc=
-----END CERTIFICATE-----
)";

const std::string SRV_PEM = R"(-----BEGIN CERTIFICATE-----
MIIBbzCCARSgAwIBAgIUEJLlxhdAFWybXLGGEov1EA7oeUswCgYIKoZIzj0EAwIw
GDEWMBQGA1UEAwwNVGVzdCBMb2NhbCBDQTAgFw0yNjExMTIxNjU2NTZaGA8yMTI2
MTAxOTE2NTY1NlowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0CAQYI
KoZIzj0DAQcDQgAEV2aNxgLvtn266d9LE1EdcYphG5GW59PbCN7m6uq7n9l599XG
XbbijNe0ieID8AO/eUGixozRXOnne5FieZ+DTaM+MDwwGgYDVR0RBBMwEYIJbG9j
YWxob3N0hwR/AAABMAkGA1UdEwQCMAAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwCgYI
KoZIzj0EAwIDSQAwRgIhAMvv6Wew/jdo8hfleTowEI2mRPgls3s/Vj2YXeSRWARu
AiEA76Eri6CcQP/k/Mvu39DrMrFAwepsXhRLsAwtqQZlYEA=
-----END CERTIFICATE-----
)";
const std::string SRV_PIN = "sha256//DhZqeK/viJ8lil8oyPOjhzewVwjWgrBfnHk9GatZceA=";

// DER of the first certificate of a PEM, through the code under test's own PEM output.
std::string der_of(const std::string& pem)
{
    std::string base64;
    bool body = false;
    for (size_t pos = 0; pos < pem.size();) {
        size_t end = pem.find('\n', pos);
        if (end == std::string::npos) {
            end = pem.size();
        }
        const std::string line = pem.substr(pos, end - pos);
        pos                    = end + 1;
        if (line.starts_with("-----BEGIN")) {
            body = true;
        } else if (line.starts_with("-----END")) {
            break;
        } else if (body) {
            base64 += line;
        }
    }
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits  = -8;
    for (char c : base64) {
        if (c == '=') {
            break;
        }
        const size_t index = alphabet.find(c);
        if (index == std::string::npos) {
            continue;
        }
        value = (value << 6) + int(index);
        bits += 6;
        if (bits >= 0) {
            out.push_back(char((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

} // namespace

TEST_CASE("Public key pin of a certificate", "[connect-local]")
{
    CHECK(Tls::spki_pin(SELF_PEM) == SELF_PIN);
    CHECK(Tls::spki_pin(SRV_PEM) == SRV_PIN);
    // DER gives the same pin.
    CHECK(Tls::spki_pin(der_of(SELF_PEM)) == SELF_PIN);
    // The first certificate of a bundle counts.
    CHECK(Tls::spki_pin(SRV_PEM + CA_PEM) == SRV_PIN);

    CHECK_FALSE(Tls::spki_pin("").has_value());
    CHECK_FALSE(Tls::spki_pin("not a certificate").has_value());
    CHECK_FALSE(
        Tls::spki_pin("-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n").has_value()
    );
}

TEST_CASE("Certificate fingerprints are colon separated upper case hex", "[connect-local]")
{
    const unsigned char bytes[] = {0x00, 0x0a, 0xff, 0x12};
    CHECK(Tls::format_fingerprint(bytes, sizeof(bytes)) == "00:0A:FF:12");
    CHECK(Tls::format_fingerprint(bytes, 1) == "00");
    CHECK(Tls::format_fingerprint(bytes, 0).empty());
}

TEST_CASE("Certificate summary for the user", "[connect-local]")
{
    const std::optional<Network::CertificateInfo> info = Tls::certificate_info(SELF_PEM);
    REQUIRE(info.has_value());
    CHECK(info->sha256_fingerprint == SELF_FINGERPRINT);
    CHECK(info->spki_pin == SELF_PIN);
    CHECK(info->subject == "CN=connect-local.test,O=Prusa Test");
    CHECK(info->issuer == info->subject);
    CHECK(
        info->subject_alt_names == std::vector<std::string>{"connect-local.test", "192.168.1.10"}
    );
    CHECK(info->not_before == "2026-11-12 16:56:56 UTC");
    CHECK(info->not_after == "2126-10-19 16:56:56 UTC");
    CHECK(info->self_signed);
    CHECK_FALSE(info->expired);

    const std::optional<Network::CertificateInfo> srv = Tls::certificate_info(der_of(SRV_PEM));
    REQUIRE(srv.has_value());
    CHECK(srv->subject == "CN=localhost");
    CHECK(srv->issuer == "CN=Test Local CA");
    CHECK_FALSE(srv->self_signed);
    CHECK(srv->subject_alt_names == std::vector<std::string>{"localhost", "127.0.0.1"});

    CHECK_FALSE(Tls::certificate_info("garbage").has_value());
}

TEST_CASE("Certificate files are read as PEM or DER", "[connect-local]")
{
    CHECK(Tls::certificates_to_pem(SELF_PEM) == SELF_PEM);
    CHECK(Tls::certificates_to_pem(der_of(SELF_PEM)) == SELF_PEM);
    CHECK(Tls::certificates_to_pem("leading text\n" + CA_PEM + SRV_PEM) == CA_PEM + SRV_PEM);
    CHECK_FALSE(Tls::certificates_to_pem("").has_value());
    CHECK_FALSE(
        Tls::certificates_to_pem("-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n")
            .has_value()
    );
}

TEST_CASE("Server certificate chains are verified against a loaded certificate", "[connect-local]")
{
    // Host names and IP addresses of the certificate.
    CHECK(Tls::chain_verifies({SRV_PEM}, CA_PEM, "localhost"));
    CHECK(Tls::chain_verifies({SRV_PEM}, CA_PEM, "LOCALHOST"));
    CHECK(Tls::chain_verifies({der_of(SRV_PEM)}, CA_PEM, "127.0.0.1"));
    CHECK_FALSE(Tls::chain_verifies({SRV_PEM}, CA_PEM, "printfarm.local"));
    CHECK_FALSE(Tls::chain_verifies({SRV_PEM}, CA_PEM, "127.0.0.2"));
    // Another CA, or none.
    CHECK_FALSE(Tls::chain_verifies({SRV_PEM}, OTHER_CA_PEM, "localhost"));
    CHECK_FALSE(Tls::chain_verifies({SRV_PEM}, "", "localhost"));
    CHECK(Tls::chain_verifies({SRV_PEM}, OTHER_CA_PEM + CA_PEM, "localhost"));
    // The server certificate itself loaded as the anchor.
    CHECK(Tls::chain_verifies({SRV_PEM}, SRV_PEM, "localhost"));
    CHECK(Tls::chain_verifies({SELF_PEM}, SELF_PEM, "connect-local.test"));
    CHECK(Tls::chain_verifies({SELF_PEM}, SELF_PEM, "[192.168.1.10]"));
    CHECK_FALSE(Tls::chain_verifies({SELF_PEM}, SELF_PEM, "192.168.1.11"));
    // The CA sent along by the server does not make it trusted.
    CHECK_FALSE(Tls::chain_verifies({SRV_PEM, CA_PEM}, OTHER_CA_PEM, "localhost"));
    CHECK_FALSE(Tls::chain_verifies({}, CA_PEM, "localhost"));
}

TEST_CASE("A custom trust accepts only its certificate", "[connect-local]")
{
    ServerTrust pinned{ServerTrustKind::PinnedKey, "https://192.168.1.10", SELF_PIN, {}, {}};
    // The pin ignores the host name.
    CHECK(pinned.accepts_chain({SELF_PEM}, "192.168.1.99"));
    CHECK(pinned.accepts_chain({der_of(SELF_PEM), CA_PEM}, "anything"));
    CHECK_FALSE(pinned.accepts_chain({SRV_PEM}, "192.168.1.10"));
    CHECK_FALSE(pinned.accepts_chain({}, "192.168.1.10"));
    pinned.pin.clear();
    CHECK_FALSE(pinned.accepts_chain({SELF_PEM}, "192.168.1.10"));

    ServerTrust
        loaded{ServerTrustKind::Certificate, "https://localhost:8443", {}, "ca.pem", CA_PEM};
    CHECK(loaded.accepts_chain({SRV_PEM}, "localhost"));
    CHECK_FALSE(loaded.accepts_chain({SRV_PEM}, "printfarm.local"));
    CHECK_FALSE(loaded.accepts_chain({SELF_PEM}, "connect-local.test"));
    // The certificate file could not be read.
    loaded.pem.clear();
    CHECK_FALSE(loaded.accepts_chain({SRV_PEM}, "localhost"));

    CHECK_FALSE(ServerTrust{}.accepts_chain({SRV_PEM}, "localhost"));
}

TEST_CASE("A newer certificate check replaces the running one", "[connect-local]")
{
    using Clock = std::chrono::steady_clock;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
    const auto dispatch_for = [&dispatcher](std::chrono::milliseconds duration, auto&& done)
    {
        const auto end = Clock::now() + duration;
        while (!done() && Clock::now() < end) {
            dispatcher.dispatch_enqueued();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    std::vector<std::string> results;
    Network::ServerTrustProbe probe("Test certificate check");
    // Not routable: it would wait for the connect timeout, it is cancelled instead.
    probe.start(
        "https://10.255.255.1:8443",
        {},
        [&results](Network::TlsProbeResult) { results.emplace_back("first"); }
    );
    // Nothing listens there: refused right away.
    probe.start(
        "https://127.0.0.1:1",
        {},
        [&results](Network::TlsProbeResult result)
        {
            results.emplace_back(
                result.status == Network::TlsProbeStatus::Unreachable ? "second" : "?"
            );
        }
    );
    dispatch_for(std::chrono::seconds(10), [&results]() { return !results.empty(); });
    // The cancelled check ends within about a second, its result is dropped.
    dispatch_for(std::chrono::milliseconds(1500), []() { return false; });
    CHECK(results == std::vector<std::string>{"second"});
}

// Manual check against a running https server, e.g. a Connect Local demo with a self-signed
// certificate: CONNECT_LOCAL_TLS_URL=https://localhost:8443 slic3r-shared-tests "[connect-local-live]"
// (optionally CONNECT_LOCAL_TLS_CA=<the CA file> to check a loaded certificate too).
TEST_CASE("Live: certificate probe and pinned requests", "[.][connect-local-live]")
{
    const char* env = std::getenv("CONNECT_LOCAL_TLS_URL");
    if (env == nullptr || *env == '\0') {
        SKIP("CONNECT_LOCAL_TLS_URL not set.");
    }
    const std::string url =
        ServiceConfig::normalize_offline_server_url(env).value_or(std::string());
    REQUIRE(url.starts_with("https://"));

    const Network::TlsProbeResult system = Network::probe_server_certificate(url, {});
    INFO("system trust: " << system.error);
    if (system.status == Network::TlsProbeStatus::Ok) {
        SUCCEED("Trusted by the system.");
        return;
    }
    REQUIRE(system.status == Network::TlsProbeStatus::CertificateRejected);
    REQUIRE(system.certificate.has_value());
    UNSCOPED_INFO(
        "fingerprint "
        << system.certificate->sha256_fingerprint
        << " pin "
        << system.certificate->spki_pin
    );

    // CONNECT_LOCAL_TLS_CA: the CA (or server) certificate file, verified with the host name.
    if (const char* ca = std::getenv("CONNECT_LOCAL_TLS_CA"); ca != nullptr && *ca != '\0') {
        const ServerTrust loaded{ServerTrustKind::Certificate, url, {}, ca, {}};
        const Network::TlsProbeResult result = Network::probe_server_certificate(url, loaded);
        INFO("loaded certificate: " << result.error);
        CHECK(result.status == Network::TlsProbeStatus::Ok);
    }

    ServerTrust pinned{ServerTrustKind::PinnedKey, url, system.certificate->spki_pin, {}, {}};
    CHECK(Network::probe_server_certificate(url, pinned).status == Network::TlsProbeStatus::Ok);
    ServerTrust wrong = pinned;
    wrong.pin         = SELF_PIN;
    CHECK(
        Network::probe_server_certificate(url, wrong).status
        == Network::TlsProbeStatus::CertificateRejected
    );
}

// Manual check that the probe always ends, against the demos and a closed port:
// CONNECT_LOCAL_HTTP_URL=http://127.0.0.1:18398 CONNECT_LOCAL_TLS_URL=https://127.0.0.1:18443 slic3r-shared-tests "[connect-local-live]"
TEST_CASE("Live: the certificate probe always ends", "[.][connect-local-live]")
{
    using Clock      = std::chrono::steady_clock;
    const auto timed = [](const std::string& url, const ServerTrust& trust)
    {
        const auto start               = Clock::now();
        Network::TlsProbeResult result = Network::probe_server_certificate(url, trust);
        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count();
        return std::make_pair(result, seconds);
    };

    if (const char* http = std::getenv("CONNECT_LOCAL_HTTP_URL"); http != nullptr && *http != '\0')
    {
        const auto [result, seconds] = timed(http, {});
        CHECK(result.status == Network::TlsProbeStatus::Ok);
        CHECK(seconds <= 1);
    }
    if (const char* tls = std::getenv("CONNECT_LOCAL_TLS_URL"); tls != nullptr && *tls != '\0') {
        const std::string url =
            ServiceConfig::normalize_offline_server_url(tls).value_or(std::string());
        const auto [result, seconds] = timed(url, {});
        INFO(result.error);
        CHECK(result.status != Network::TlsProbeStatus::Unreachable);
        CHECK(seconds <= 10);
    }
    // Nothing listens there: refused right away, reported as unreachable with curl's reason.
    const auto [closed, seconds] = timed("https://127.0.0.1:1", {});
    CHECK(closed.status == Network::TlsProbeStatus::Unreachable);
    CHECK_FALSE(closed.error.empty());
    CHECK(seconds <= 10);
}
