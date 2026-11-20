#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/PrintHost/PrintHostPrusaConnectLocal.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostFactory.hpp"
#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Slic3r::Biz;
using Slic3r::Biz::Network::IHttp;
using Slic3r::Biz::Network::MockHttp;
namespace fs = boost::filesystem;

namespace {

constexpr const char* HOST         = "http://pcl.local:18399/api/v1/modules/compat/connect";
constexpr const char* TEAM_ID      = "1";
constexpr const char* PRINTER_UUID = "uuid-1";
constexpr const char* TOKEN        = "tok";
const std::string FILE_CONTENT     = "0123456789012345678901234567890123456789";

struct SeenRequest
{
    IHttp::RequestMethod method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string post_body;
    std::string put_path;
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
    std::vector<std::string> unauthorized_tokens;
    std::vector<std::string> infos;
    bool cancel_on_progress{false};
    std::string team_id{TEAM_ID};

    StepReply register_reply{
        201,
        R"({"id":1234,"team_id":1,"name":"print.bgcode","size":40,"state":"INITIATED","path":"/usb/print.bgcode"})"
    };
    StepReply put_reply{200, ""};

    Fixture()
    {
        source_path = fs::temp_directory_path() / fs::unique_path("slic3r-connect-local-%%%%-%%%%.bgcode");
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
                expectations.push_back(NAMED_ALLOW_CALL(mock, set_post_body_str_mock(trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].post_body = _1));
                expectations.push_back(NAMED_ALLOW_CALL(mock, set_put_body_mock(trompeloeil::_))
                                           .SIDE_EFFECT(seen[index].put_path = _1));

                http->set_perform_override_fn(
                    [this, index](MockHttp* performed, const Network::HttpRetryOpt&)
                    {
                        if (seen[index].method == IHttp::RequestMethod::Put) {
                            if (performed->invoke_progress(FILE_CONTENT.size(), FILE_CONTENT.size() / 4)) {
                                return;
                            }
                        }
                        const StepReply& reply = seen[index].method == IHttp::RequestMethod::Put ? put_reply : register_reply;
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

    PhysicalPrinter::PhysicalPrinterConfig config()
    {
        PhysicalPrinter::PhysicalPrinterConfig config;
        config.host = HOST;
        PhysicalPrinter::ConnectUpload upload;
        upload.api             = PhysicalPrinter::ConnectUploadApi::Legacy;
        upload.team_id         = team_id;
        upload.printer_uuid    = PRINTER_UUID;
        upload.access_token    = TOKEN;
        upload.on_unauthorized = [this](const std::string& token) { unauthorized_tokens.push_back(token); };
        config.payload = std::move(upload);
        return config;
    }

    PrintHost::PrintHostJobData job_data() const
    {
        PrintHost::PrintHostJobData data{std::monostate{}, fs::path("print.bgcode")};
        data.source_path       = source_path;
        data.request_body_json = R"({"set_ready":true,"team_id":1,"printer_uuid":"uuid-1","filename":"print.bgcode"})";
        return data;
    }

    bool run()
    {
        PrintHost::PrintHostPrusaConnectLocal host(config(), job_data());
        return host.perform(
            [this](IHttp::Progress, bool& cancel)
            {
                if (cancel_on_progress) {
                    cancel = true;
                }
            },
            [](IHttp::Retry, bool&) {},
            [this](std::string msg) { errors.push_back(std::move(msg)); },
            [this](PrintHost::PrintHostJobInfoTag, std::string info) { infos.push_back(std::move(info)); }
        );
    }
};

} // namespace

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: register then PUT the file", "[PrintHost][PrusaConnectLocal]")
{
    REQUIRE(run());
    REQUIRE(seen.size() == 2);
    CHECK(errors.empty());

    const SeenRequest& reg = seen[0];
    CHECK(reg.method == IHttp::RequestMethod::Post);
    CHECK(reg.url == std::string(HOST) + "/app/users/teams/1/uploads");
    CHECK(reg.headers.at("Authorization") == "Bearer tok");
    CHECK(reg.headers.at("Content-Type") == "application/json");
    const nlohmann::json body = nlohmann::json::parse(reg.post_body);
    CHECK(body["filename"] == "print.bgcode");
    CHECK(body["size"] == FILE_CONTENT.size());
    CHECK(body["set_ready"] == true);

    const SeenRequest& put = seen[1];
    CHECK(put.method == IHttp::RequestMethod::Put);
    CHECK(put.url == std::string(HOST) + "/app/teams/1/files/raw?upload_id=1234");
    CHECK(put.headers.at("Authorization") == "Bearer tok");
    CHECK(put.headers.at("Content-Type") == "text/x.gcode");
    CHECK(put.put_path == source_path.string());
}

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: missing team id fails before any request", "[PrintHost][PrusaConnectLocal]")
{
    team_id.clear();
    CHECK_FALSE(run());
    CHECK(seen.empty());
    REQUIRE(errors.size() == 1);
}

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: server error message is reported", "[PrintHost][PrusaConnectLocal]")
{
    register_reply = {400, R"({"message":"Printer is busy"})"};
    CHECK_FALSE(run());
    CHECK(seen.size() == 1);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == "Printer is busy");
    CHECK(unauthorized_tokens.empty());
}

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: 401 reports the refused token", "[PrintHost][PrusaConnectLocal]")
{
    register_reply = {401, R"({"code":"TOKEN_INVALID"})"};
    CHECK_FALSE(run());
    REQUIRE(unauthorized_tokens.size() == 1);
    CHECK(unauthorized_tokens[0] == TOKEN);
}

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: reply without an upload id fails", "[PrintHost][PrusaConnectLocal]")
{
    register_reply = {201, R"({"team_id":1})"};
    CHECK_FALSE(run());
    CHECK(seen.size() == 1);
    REQUIRE(errors.size() == 1);
}

TEST_CASE_METHOD(Fixture, "PrusaConnectLocal upload: cancel during the file upload", "[PrintHost][PrusaConnectLocal]")
{
    cancel_on_progress = true;
    CHECK_FALSE(run());
    CHECK(seen.size() == 2);
    CHECK(errors.empty());
}

TEST_CASE_METHOD(Fixture, "Connect upload API selects the print host", "[PrintHost][PrusaConnectLocal]")
{
    std::unique_ptr<PrintHost::IPrintHost> local = PrintHost::create_print_host(config(), job_data());
    REQUIRE(local);
    CHECK(std::string(local->get_name()) == "PrusaConnectLocal");

    PhysicalPrinter::PhysicalPrinterConfig cloud_config = config();
    std::get<PhysicalPrinter::ConnectUpload>(cloud_config.payload).api = PhysicalPrinter::ConnectUploadApi::SlicerRest;
    std::unique_ptr<PrintHost::IPrintHost> cloud = PrintHost::create_print_host(std::move(cloud_config), job_data());
    REQUIRE(cloud);
    CHECK(std::string(cloud->get_name()) == "PrusaConnect");
}
