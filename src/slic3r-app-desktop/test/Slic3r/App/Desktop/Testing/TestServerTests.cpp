#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Desktop/Testing/TestServer.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

using namespace Slic3r::App::Desktop::Testing;

namespace {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;

struct ClientResponse
{
    int status = 0;
    std::string body;
    std::string www_authenticate;
};

std::string basic_auth(const std::string& credentials)
{
    std::string encoded(beast::detail::base64::encoded_size(credentials.size()), '\0');
    encoded.resize(
        beast::detail::base64::encode(encoded.data(), credentials.data(), credentials.size())
    );
    return "Basic " + encoded;
}

ClientResponse send_request(
    uint16_t port,
    http::verb method,
    const std::string& target,
    const std::optional<std::string>& credentials,
    const std::string& body = {}
)
{
    asio::io_context io;
    beast::tcp_stream stream(io);
    stream.connect(asio::ip::tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"), port));

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    if (credentials) {
        request.set(http::field::authorization, basic_auth(*credentials));
    }
    if (!body.empty()) {
        request.body() = body;
        request.prepare_payload();
    }
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    return ClientResponse{
        static_cast<int>(response.result_int()),
        response.body(),
        std::string(response[http::field::www_authenticate]),
    };
}

struct ServerFixture
{
    ServerFixture(std::string password = "secret") : server(0, std::move(password))
    {
        server.add_route(
            "GET",
            "/status",
            [](const HttpRequest& request)
            {
                nlohmann::json query = nlohmann::json::object();
                for (const auto& [key, value] : request.query) {
                    query[key] = value;
                }
                return HttpResponse{200, nlohmann::json{{"ok", true}, {"query", query}}.dump()};
            }
        );
        server.add_route(
            "POST",
            "/echo",
            [](const HttpRequest& request) { return HttpResponse{200, request.body}; }
        );
        server.add_route(
            "GET",
            "/throw",
            [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("handler failed"); }
        );
        REQUIRE(server.start());
        REQUIRE(server.port() != 0);
    }

    TestServer server;
};

} // namespace

TEST_CASE("TestServer: correct password gets 200", "[testserver]")
{
    ServerFixture fixture;
    const ClientResponse response =
        send_request(fixture.server.port(), http::verb::get, "/status", "user:secret");
    REQUIRE(response.status == 200);
    REQUIRE(nlohmann::json::parse(response.body)["ok"] == true);
}

TEST_CASE("TestServer: missing or wrong password gets 401 with challenge", "[testserver]")
{
    ServerFixture fixture;

    const ClientResponse missing =
        send_request(fixture.server.port(), http::verb::get, "/status", std::nullopt);
    REQUIRE(missing.status == 401);
    REQUIRE(missing.www_authenticate == "Basic realm=\"yogatest\"");
    REQUIRE(nlohmann::json::parse(missing.body).contains("error"));

    const ClientResponse wrong =
        send_request(fixture.server.port(), http::verb::get, "/status", "user:nope");
    REQUIRE(wrong.status == 401);
    REQUIRE(wrong.www_authenticate == "Basic realm=\"yogatest\"");

    const ClientResponse unknown_route =
        send_request(fixture.server.port(), http::verb::get, "/nothing", std::nullopt);
    REQUIRE(unknown_route.status == 401);
}

TEST_CASE("TestServer: username is ignored", "[testserver]")
{
    ServerFixture fixture;
    REQUIRE(
        send_request(fixture.server.port(), http::verb::get, "/status", ":secret").status == 200
    );
    REQUIRE(
        send_request(fixture.server.port(), http::verb::get, "/status", "anybody:secret").status
        == 200
    );
}

TEST_CASE("TestServer: password may contain colons", "[testserver]")
{
    ServerFixture fixture("a:b:c");
    REQUIRE(
        send_request(fixture.server.port(), http::verb::get, "/status", "x:a:b:c").status == 200
    );
    REQUIRE(send_request(fixture.server.port(), http::verb::get, "/status", "x:a:b").status == 401);
}

TEST_CASE("TestServer: unknown path gets 404 and wrong method gets 405", "[testserver]")
{
    ServerFixture fixture;

    const ClientResponse not_found =
        send_request(fixture.server.port(), http::verb::get, "/nothing", "x:secret");
    REQUIRE(not_found.status == 404);
    REQUIRE(nlohmann::json::parse(not_found.body).contains("error"));

    const ClientResponse wrong_method =
        send_request(fixture.server.port(), http::verb::post, "/status", "x:secret", "{}");
    REQUIRE(wrong_method.status == 405);
    REQUIRE(nlohmann::json::parse(wrong_method.body).contains("error"));
}

TEST_CASE("TestServer: query string is parsed and routing ignores it", "[testserver]")
{
    ServerFixture fixture;
    const ClientResponse response = send_request(
        fixture.server.port(),
        http::verb::get,
        "/status?include_hidden=1&name=a%20b+c&flag",
        "x:secret"
    );
    REQUIRE(response.status == 200);
    const nlohmann::json query = nlohmann::json::parse(response.body)["query"];
    REQUIRE(query["include_hidden"] == "1");
    REQUIRE(query["name"] == "a b c");
    REQUIRE(query["flag"] == "");
}

TEST_CASE("TestServer: POST body reaches the handler", "[testserver]")
{
    ServerFixture fixture;
    const std::string body = R"({"action":"move","x":1,"y":2})";
    const ClientResponse response =
        send_request(fixture.server.port(), http::verb::post, "/echo", "x:secret", body);
    REQUIRE(response.status == 200);
    REQUIRE(response.body == body);
}

TEST_CASE("TestServer: throwing handler gets 500 with the message", "[testserver]")
{
    ServerFixture fixture;
    const ClientResponse response =
        send_request(fixture.server.port(), http::verb::get, "/throw", "x:secret");
    REQUIRE(response.status == 500);
    REQUIRE(nlohmann::json::parse(response.body)["error"] == "handler failed");
}

TEST_CASE("TestServer: stop returns promptly while idle and is idempotent", "[testserver]")
{
    ServerFixture fixture;
    REQUIRE(
        send_request(fixture.server.port(), http::verb::get, "/status", "x:secret").status == 200
    );

    const auto start = std::chrono::steady_clock::now();
    fixture.server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(1));
    REQUIRE(fixture.server.stopping());

    fixture.server.stop();
}

TEST_CASE("TestServer: bind failure is reported when the port is taken", "[testserver]")
{
    ServerFixture fixture;
    TestServer second(fixture.server.port(), "secret");
    REQUIRE_FALSE(second.start());
}
