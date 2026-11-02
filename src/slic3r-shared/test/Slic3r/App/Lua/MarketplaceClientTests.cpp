#include "Slic3r/App/Lua/MarketplaceClient.hpp"

#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Directories.hpp"

#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem/operations.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

/*
 * MarketplaceClient drives verify -> download -> listeners over IHttp. Mocked
 * here so the two failure modes that used to be invisible stay covered:
 *
 *   - a successful download must notify listeners exactly once (it used to
 *     re-enter perform_download_step() from its own on_result, downloading and
 *     installing the plugin in an endless loop)
 *   - HttpCurl reports HTTP >= 400 with an EMPTY error string and the payload in
 *     the body, so reporting `error` verbatim produced empty error messages
 */

namespace fs = boost::filesystem;

using Slic3r::App::Lua::BundleRequestConfig;
using Slic3r::App::Lua::IMarketplaceClientListener;
using Slic3r::App::Lua::MarketplaceClient;
using Slic3r::App::Lua::VerifiedBundleData;
using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::Platform::PlatformServices;
using Slic3r::Biz::Platform::JobManager::JobManager;

namespace {

constexpr auto VERIFY_URL   = "http://localhost:4455/api/slicer/v1/plugins/com.test.plugin/verify?version=1.0.0";
constexpr auto DOWNLOAD_URL = "http://localhost:4455/api/slicer/v1/plugins/com.test.plugin/download?version=1.0.0";
constexpr auto ZIP_BODY     = "PK\x03\x04-not-a-real-zip-but-non-empty";

BundleRequestConfig test_config()
{
    BundleRequestConfig config;
    config.plugin_id    = "com.test.plugin";
    config.version      = "1.0.0";
    config.verify_url   = VERIFY_URL;
    config.download_url = DOWNLOAD_URL;
    config.api_origin   = "http://localhost:4455";
    return config;
}

struct RecordingListener : IMarketplaceClientListener
{
    std::vector<std::string> verified;
    std::vector<std::string> downloaded;
    std::vector<std::string> errors;

    void on_download_error(const std::string&, const std::string& message) override
    {
        errors.push_back(message);
    }

    bool on_bundle_verified(const VerifiedBundleData& data) override
    {
        verified.push_back(data.plugin_id + "@" + data.version);
        return true;
    }
    void on_bundle_downloaded(const std::string&, const std::string&, const std::string& zip_path)
        override
    {
        downloaded.push_back(zip_path);
    }
};

/** What the mocked verify / download endpoints answer with. */
struct Responses
{
    unsigned verify_status{200};
    std::string verify_body{
        R"({"plugin_id":"com.test.plugin","version":"1.0.0","ok":true,"public_key":"-----BEGIN PUBLIC KEY-----\nstub\n-----END PUBLIC KEY-----\n"})"
    };
    unsigned download_status{200};
    std::string download_body{ZIP_BODY};
    /** Requests the mock saw, in order. */
    std::shared_ptr<std::vector<std::string>> seen{std::make_shared<std::vector<std::string>>()};
};

struct Fixture
{
    std::string prev_data_dir;
    fs::path temp_data_dir;
    StdMainThreadDispatcher dispatcher;
    RecordingListener listener;
    MarketplaceClient client;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
    Fixture()
    {
        prev_data_dir = Slic3r::data_dir();
        temp_data_dir = fs::temp_directory_path() / fs::unique_path("slic3r-mkt-%%%%-%%%%");
        fs::create_directories(temp_data_dir);
        Slic3r::set_data_dir(temp_data_dir.string());

        Slic3r::Biz::Network::configure_http_factory_with_mock();
        PlatformServices::instance().set_job_manager(std::make_unique<JobManager>(dispatcher));
        client.add_listener<IMarketplaceClientListener>(&listener);
    }

    ~Fixture()
    {
        client.remove_listener<IMarketplaceClientListener>(&listener);
        PlatformServices::instance().set_job_manager(nullptr);
        dispatcher.close();
        Slic3r::set_data_dir(prev_data_dir);
        boost::system::error_code ec;
        fs::remove_all(temp_data_dir, ec);
    }

    void arrange_http(const Responses& responses)
    {
        Slic3r::Biz::Network::MockHttpProvider::get().set_create_fn(
            [responses, this](
                Slic3r::Biz::Network::IHttp::RequestMethod,
                const std::string& url,
                Slic3r::Biz::Network::IHttp::RetryFn retryfn
            ) -> std::unique_ptr<Slic3r::Biz::Network::MockHttp>
            {
                auto http = std::make_unique<Slic3r::Biz::Network::MockHttp>(url, retryfn);
                expectations.push_back(NAMED_ALLOW_CALL(*http, size_limit_mock(trompeloeil::_)));
                http->set_perform_override_fn(
                    [responses,
                     url](Slic3r::Biz::Network::MockHttp* h, const Slic3r::Biz::Network::HttpRetryOpt&)
                    {
                        responses.seen->push_back(url);
                        const bool is_verify = url.find("/verify") != std::string::npos;
                        const unsigned status = is_verify ? responses.verify_status
                                                          : responses.download_status;
                        const std::string& body = is_verify ? responses.verify_body
                                                            : responses.download_body;
                        if (status >= 400) {
                            // Exactly how HttpCurl reports it: empty error, body carries the payload.
                            h->invoke_error(body, std::string{}, status);
                        } else {
                            h->invoke_complete(body, status);
                        }
                    }
                );
                return http;
            }
        );
    }

    /** Pump the main thread until `done` or the deadline. */
    void pump_until(const std::function<bool()>& done, std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            dispatcher.dispatch_enqueued();
            if (done()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        dispatcher.dispatch_enqueued();
    }

    void pump_for(std::chrono::milliseconds budget)
    {
        pump_until([] { return false; }, budget);
    }
};

} // namespace

TEST_CASE_METHOD(Fixture, "Successful install notifies listeners exactly once", "[marketplace]")
{
    Responses responses;
    arrange_http(responses);

    client.download_bundle(test_config());
    pump_until([&] { return !listener.downloaded.empty(); }, std::chrono::seconds(10));

    REQUIRE(listener.errors.empty());
    REQUIRE(listener.verified == std::vector<std::string>{"com.test.plugin@1.0.0"});
    REQUIRE(listener.downloaded.size() == 1);
    CHECK(fs::exists(listener.downloaded.front()));
    CHECK(fs::file_size(listener.downloaded.front()) == std::string{ZIP_BODY}.size());

    // The regression: perform_download_step() used to call itself from on_result,
    // so the bundle was downloaded and installed over and over. Keep pumping and
    // make sure nothing else arrives.
    pump_for(std::chrono::milliseconds(1500));
    CHECK(listener.downloaded.size() == 1);
    CHECK(listener.verified.size() == 1);
    CHECK(listener.errors.empty());
}

TEST_CASE_METHOD(Fixture, "A 404 on download is reported with a usable message", "[marketplace]")
{
    Responses responses;
    responses.download_status = 404;
    responses.download_body   = "Not Found";
    arrange_http(responses);

    client.download_bundle(test_config());
    pump_until([&] { return !listener.errors.empty(); }, std::chrono::seconds(10));

    REQUIRE(listener.errors.size() == 1);
    const std::string& message = listener.errors.front();
    CAPTURE(message);
    // Used to be empty: HttpCurl passes "" as `error` for HTTP >= 400.
    CHECK_FALSE(message.empty());
    CHECK(message.find("404") != std::string::npos);
    CHECK(message.find("Not Found") != std::string::npos);
    CHECK(listener.downloaded.empty());
}

TEST_CASE_METHOD(Fixture, "A refused signature stops before downloading", "[marketplace]")
{
    Responses responses;
    responses.verify_body = R"({"plugin_id":"com.test.plugin","version":"1.0.0","ok":false,"reason":"signing key revoked"})";
    arrange_http(responses);

    client.download_bundle(test_config());
    pump_until([&] { return !listener.errors.empty(); }, std::chrono::seconds(10));

    REQUIRE(listener.errors.size() == 1);
    CAPTURE(listener.errors.front());
    CHECK(listener.errors.front().find("signing key revoked") != std::string::npos);
    CHECK(listener.verified.empty());
    CHECK(listener.downloaded.empty());
    // Nothing may be fetched from the download endpoint after a refused verify.
    for (const auto& url : *responses.seen) {
        CHECK(url.find("/download") == std::string::npos);
    }
}

TEST_CASE_METHOD(Fixture, "A 404 on verify is reported and stops the install", "[marketplace]")
{
    Responses responses;
    responses.verify_status = 404;
    responses.verify_body   = R"({"error":"version not found"})";
    arrange_http(responses);

    client.download_bundle(test_config());
    pump_until([&] { return !listener.errors.empty(); }, std::chrono::seconds(10));

    // Verify had no on_error handler at all before, so this failed silently.
    REQUIRE(listener.errors.size() == 1);
    CAPTURE(listener.errors.front());
    CHECK(listener.errors.front().find("404") != std::string::npos);
    CHECK(listener.downloaded.empty());
}
