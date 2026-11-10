#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/PrintHost/PrintHostPrusaConnect.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Slic3r::Biz;
using Slic3r::Biz::Network::IHttp;
using Slic3r::Biz::Network::MockHttp;
namespace fs = boost::filesystem;

namespace {

constexpr const char* UPLOAD_URL   = "https://storage.googleapis.com/bucket/object?X-Goog-Signature=abc";
constexpr const char* FILE_ID      = "2dd9f156-9ec0-4266-9cb7-29ead99a121c";
constexpr const char* ORG_ID       = "42";
constexpr const char* PRINTER_UUID = "uuid-1";
constexpr const char* TOKEN        = "tok";
const std::string FILE_CONTENT     = "0123456789012345678901234567890123456789";

struct SeenRequest
{
    IHttp::RequestMethod method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::vector<std::string> removed_headers;
    std::string post_body;
    std::string put_path;
    std::chrono::milliseconds initial_delay{0};

    bool has_header(const std::string& name) const
    {
        return headers.count(name) > 0;
    }

    bool removed(const std::string& name) const
    {
        return std::find(removed_headers.begin(), removed_headers.end(), name) != removed_headers.end();
    }
};

struct StepReply
{
    unsigned status{200};
    std::string body;
};

struct Fixture
{
    fs::path source_path;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
    std::vector<SeenRequest> seen;
    std::vector<std::string> errors;
    std::atomic<bool> stop{false};
    bool report_progress{true};
    bool cancel_on_progress{false};
    size_t progress_calls{0};
    std::string organization_id{ORG_ID};
    std::string request_body_json{
        R"({"set_ready":true,"team_id":10533,"printer_uuid":"uuid-1","filename":"print.bgcode"})"
    };

    StepReply create_reply{
        201,
        R"({"file":{"id":"2dd9f156-9ec0-4266-9cb7-29ead99a121c","name":"print.bgcode","size":40,"status":"PENDING"},)"
        R"("upload_url":"https://storage.googleapis.com/bucket/object?X-Goog-Signature=abc",)"
        R"("upload_headers":{"x-goog-content-length-range":"40,40"}})"
    };
    StepReply put_reply{200, ""};
    StepReply finish_reply{
        200,
        R"({"id":"2dd9f156-9ec0-4266-9cb7-29ead99a121c","name":"print.bgcode","size":40,"status":"PENDING"})"
    };
    StepReply queue_reply{201, R"({"job_id":"8f1c2a3e-5b7d-4c9e-a1f2-3b4c5d6e7f80","transfer_id":"42"})"};

    Fixture()
    {
        source_path = fs::temp_directory_path() / fs::unique_path("slic3r-connect-%%%%-%%%%.bgcode");
        {
            boost::nowide::ofstream out(source_path.string(), std::ios::binary);
            out << FILE_CONTENT;
        }

        Network::configure_http_factory_with_mock();
        Network::MockHttpProvider::get().set_create_fn(
            [this](IHttp::RequestMethod method, const std::string& url, IHttp::RetryFn retry_fn)
            {
                auto http          = std::make_unique<MockHttp>(method, url, std::move(retry_fn));
                const size_t index = seen.size();
                seen.push_back(SeenRequest{method, url});

                MockHttp& mock = *http;
                expectations.push_back(NAMED_ALLOW_CALL(mock, header_mock(trompeloeil::_, trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].headers[_1] = _2));
                expectations.push_back(NAMED_ALLOW_CALL(mock, remove_header_mock(trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].removed_headers.push_back(_1)));
                expectations.push_back(NAMED_ALLOW_CALL(mock, set_post_body_str_mock(trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].post_body = _1));
                expectations.push_back(NAMED_ALLOW_CALL(mock, set_put_body_mock(trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].put_path = _1));
                expectations.push_back(NAMED_ALLOW_CALL(mock, size_limit_mock(trompeloeil::_)));
                expectations.push_back(NAMED_ALLOW_CALL(mock, timeout_total_mock(trompeloeil::_)));
                expectations.push_back(NAMED_ALLOW_CALL(mock, timeout_connection_mock(trompeloeil::_)));

                http->set_perform_override_fn(
                    [this, index](MockHttp* performed, const Network::HttpRetryOpt& opts)
                    {
                        seen[index].initial_delay = opts.initial_delay;
                        if (seen[index].method == IHttp::RequestMethod::Put) {
                            if (performed->invoke_progress(FILE_CONTENT.size(), FILE_CONTENT.size() / 4)) {
                                return;
                            }
                        }
                        const StepReply& reply = reply_for(seen[index]);
                        if (reply.status >= 400) {
                            performed->invoke_error(reply.body, std::string{}, reply.status);
                        } else {
                            performed->invoke_complete(reply.body, reply.status);
                        }
                    }
                );
                return http;
            }
        );
    }

    ~Fixture()
    {
        Network::MockHttpProvider::get().set_create_fn({});
        boost::system::error_code ec;
        fs::remove(source_path, ec);
    }

    const StepReply& reply_for(const SeenRequest& request) const
    {
        if (request.method == IHttp::RequestMethod::Put) {
            return put_reply;
        }
        if (request.method == IHttp::RequestMethod::Patch) {
            return finish_reply;
        }
        if (request.url.find("/queue") != std::string::npos) {
            return queue_reply;
        }
        return create_reply;
    }

    bool run()
    {
        PhysicalPrinter::PhysicalPrinterConfig config;
        config.host = "connect";
        PhysicalPrinter::ConnectUpload upload;
        upload.organization_id = organization_id;
        upload.printer_uuid    = PRINTER_UUID;
        upload.access_token    = TOKEN;
        config.payload         = std::move(upload);

        PrintHost::PrintHostJobData data{std::monostate{}, fs::path("print.bgcode")};
        data.source_path       = source_path;
        data.request_body_json = request_body_json;

        IHttp::ProgressFn progress_fn;
        if (report_progress) {
            progress_fn = [this](IHttp::Progress, bool& cancel)
            {
                ++progress_calls;
                if (cancel_on_progress) {
                    cancel = true;
                }
            };
        }

        PrintHost::PrintHostPrusaConnect host(std::move(config), std::move(data));
        return host.perform(
            progress_fn,
            [this](IHttp::Retry, bool& cancel)
            {
                if (stop) {
                    cancel = true;
                }
            },
            [this](std::string msg) { errors.push_back(std::move(msg)); },
            [](PrintHost::PrintHostJobInfoTag, std::string) {}
        );
    }
};

} // namespace

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: four steps in order", "[PrintHost][PrusaConnect]")
{
    const Network::ServiceConfig& urls = Network::ServiceConfig::instance();

    REQUIRE(run());
    CHECK(errors.empty());
    REQUIRE(seen.size() == 4);

    SECTION("create file record") {
        const SeenRequest& create = seen[0];
        CHECK(create.method == IHttp::RequestMethod::Post);
        CHECK(create.url == urls.connect_organization_files_url(ORG_ID));
        CHECK(create.headers.at("Authorization") == std::string("Bearer ") + TOKEN);
        CHECK(create.headers.at("Content-Type") == "application/json");
        const nlohmann::json body = nlohmann::json::parse(create.post_body);
        CHECK(body["name"] == "print.bgcode");
        CHECK(body["size"] == FILE_CONTENT.size());
    }

    SECTION("bucket upload has no token and drives progress") {
        const SeenRequest& put = seen[1];
        CHECK(put.method == IHttp::RequestMethod::Put);
        CHECK(put.url == UPLOAD_URL);
        CHECK_FALSE(put.has_header("Authorization"));
        CHECK(put.headers.at("Content-Type") == "application/octet-stream");
        CHECK(put.headers.at("x-goog-content-length-range") == "40,40");
        CHECK(put.removed("Expect"));
        CHECK(put.put_path == source_path.string());
        CHECK(put.post_body.empty());
        CHECK(put.initial_delay == std::chrono::milliseconds(0));
        CHECK(progress_calls > 0);
    }

    SECTION("finish file record") {
        const SeenRequest& finish = seen[2];
        CHECK(finish.method == IHttp::RequestMethod::Patch);
        CHECK(finish.url == urls.connect_organization_file_url(ORG_ID, FILE_ID));
        CHECK(finish.headers.at("Authorization") == std::string("Bearer ") + TOKEN);
        CHECK(finish.headers.at("Content-Type") == "application/json");
        const nlohmann::json body = nlohmann::json::parse(finish.post_body);
        CHECK(body.size() == 1);
        REQUIRE(body["upload_finished_at"].is_string());
        const std::string finished_at = body["upload_finished_at"].get<std::string>();
        CHECK(finished_at.size() == std::string("2026-01-01T00:00:00Z").size());
        CHECK(finished_at.back() == 'Z');
    }

    SECTION("queue on printer passes the web view data through") {
        const SeenRequest& queue = seen[3];
        CHECK(queue.method == IHttp::RequestMethod::Post);
        CHECK(queue.url == urls.connect_printer_queue_url(PRINTER_UUID));
        CHECK(queue.headers.at("Authorization") == std::string("Bearer ") + TOKEN);
        CHECK(queue.headers.at("Content-Type") == "application/json");
        const nlohmann::json body = nlohmann::json::parse(queue.post_body);
        CHECK(body["file_id"].is_string());
        CHECK(body["file_id"] == FILE_ID);
        CHECK(body["organization_id"] == ORG_ID);
        CHECK(body["set_ready"] == true);
        CHECK(body.size() == 3);
    }
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: queue forwards only known fields", "[PrintHost][PrusaConnect]")
{
    request_body_json = R"({"set_ready":false,"position":-1,"tool_mapping":{"1":[1,2],"2":[3]},)"
                        R"("destination":"/usb","team_id":10533,"filename":"print.bgcode","position_hint":"x"})";

    REQUIRE(run());
    REQUIRE(seen.size() == 4);
    const nlohmann::json body = nlohmann::json::parse(seen[3].post_body);
    CHECK(body["file_id"] == FILE_ID);
    CHECK(body["organization_id"] == ORG_ID);
    CHECK(body["set_ready"] == false);
    CHECK(body["position"] == -1);
    CHECK(body["tool_mapping"] == nlohmann::json::parse(R"({"1":[1,2],"2":[3]})"));
    CHECK(body["destination"] == "/usb");
    CHECK(body.size() == 6);
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: queue drops fields of a wrong type", "[PrintHost][PrusaConnect]")
{
    request_body_json = R"({"set_ready":"yes","position":"first","tool_mapping":[1],"destination":5})";

    REQUIRE(run());
    REQUIRE(seen.size() == 4);
    const nlohmann::json body = nlohmann::json::parse(seen[3].post_body);
    CHECK(body["file_id"] == FILE_ID);
    CHECK(body["organization_id"] == ORG_ID);
    CHECK(body.size() == 2);
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: incomplete create file reply", "[PrintHost][PrusaConnect]")
{
    SECTION("no upload URL") {
        create_reply.body = R"({"file":{"id":"2dd9f156-9ec0-4266-9cb7-29ead99a121c"},"upload_headers":{}})";
    }

    SECTION("file id outside the file object") {
        create_reply.body = R"({"id":"2dd9f156-9ec0-4266-9cb7-29ead99a121c","upload_url":"https://storage.googleapis.com/b/o","upload_headers":{}})";
    }

    SECTION("numeric file id") {
        create_reply.body = R"({"file":{"id":777},"upload_url":"https://storage.googleapis.com/b/o","upload_headers":{}})";
    }

    CHECK_FALSE(run());
    REQUIRE(seen.size() == 1);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("upload URL") != std::string::npos);
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: server message is shown", "[PrintHost][PrusaConnect]")
{
    SECTION("message") {
        create_reply = {403, R"({"message":"Forbidden for this organization"})"};
        CHECK_FALSE(run());
        REQUIRE(seen.size() == 1);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("Forbidden for this organization") != std::string::npos);
    }

    SECTION("detail") {
        create_reply = {422, R"({"detail":[{"type":"missing","loc":["body","name"],"msg":"Field required","input":{}}]})"};
        CHECK_FALSE(run());
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("body.name: Field required") != std::string::npos);
    }

    SECTION("unauthorized adds a login hint") {
        create_reply = {401, R"({"message":"Token expired"})"};
        CHECK_FALSE(run());
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("Token expired") != std::string::npos);
        CHECK(errors[0].find("log in") != std::string::npos);
    }

    SECTION("plain body falls back to HTTP status") {
        put_reply = {403, "<Error><Code>SignatureDoesNotMatch</Code></Error>"};
        CHECK_FALSE(run());
        REQUIRE(seen.size() == 2);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("HTTP 403") != std::string::npos);
    }

    SECTION("unauthorized bucket does not ask to log in") {
        put_reply = {401, "<Error><Code>ExpiredToken</Code></Error>"};
        CHECK_FALSE(run());
        REQUIRE(seen.size() == 2);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("HTTP 401") != std::string::npos);
        CHECK(errors[0].find("log in") == std::string::npos);
    }
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: stops at the failing step", "[PrintHost][PrusaConnect]")
{
    SECTION("finish fails") {
        finish_reply = {500, ""};
        CHECK_FALSE(run());
        CHECK(seen.size() == 3);
        CHECK(errors.size() == 1);
    }

    SECTION("finish rejected by the server") {
        finish_reply = {400, R"({"error_code":"file_not_updated","message":"No fields to update."})"};
        CHECK_FALSE(run());
        CHECK(seen.size() == 3);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("No fields to update.") != std::string::npos);
    }

    SECTION("queue fails") {
        queue_reply = {409, R"({"detail":"Printer busy"})"};
        CHECK_FALSE(run());
        CHECK(seen.size() == 4);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("Printer busy") != std::string::npos);
    }
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: missing organization", "[PrintHost][PrusaConnect]")
{
    organization_id.clear();

    CHECK_FALSE(run());
    CHECK(seen.empty());
    CHECK(errors.size() == 1);
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: cancel during the bucket upload", "[PrintHost][PrusaConnect]")
{
    cancel_on_progress = true;

    CHECK_FALSE(run());
    CHECK(seen.size() == 2);
    CHECK(errors.empty());
    CHECK(progress_calls == 1);
}

TEST_CASE_METHOD(Fixture, "PrusaConnect upload: cancel through the retry callback without progress reporting", "[PrintHost][PrusaConnect]")
{
    report_progress = false;
    stop            = true;

    CHECK_FALSE(run());
    CHECK(seen.size() == 2);
    CHECK(errors.empty());
    CHECK(progress_calls == 0);
}

TEST_CASE("PrusaConnect API URLs", "[PrintHost][PrusaConnect]")
{
    const Network::ServiceConfig& urls = Network::ServiceConfig::instance();
    const std::string& api             = urls.connect_api_url();
    REQUIRE_FALSE(api.empty());

    CHECK(urls.connect_printer_list_url() == api + "/printer/slicer/v1/printers");
    CHECK(urls.connect_organization_files_url("42") == api + "/file/slicer/v1/organizations/42/files");
    CHECK(urls.connect_organization_file_url("42", "7") == api + "/file/slicer/v1/organizations/42/files/7");
    CHECK(urls.connect_printer_queue_url("u-1") == api + "/printer/slicer/v1/printers/u-1/queue");
}
