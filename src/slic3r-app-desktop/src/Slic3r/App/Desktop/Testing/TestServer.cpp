#include "TestServer.hpp"

#include <Slic3r/Log.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace Slic3r::App::Desktop::Testing {

namespace {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp       = asio::ip::tcp;

constexpr size_t body_limit       = 1024 * 1024;
constexpr auto connection_timeout = std::chrono::seconds(5);
constexpr auto poll_interval      = std::chrono::milliseconds(50);
constexpr std::string_view basic  = "Basic ";

int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result += ' ';
        } else if (
            value[i] == '%'
            && i + 2 < value.size()
            && hex_value(value[i + 1]) >= 0
            && hex_value(value[i + 2]) >= 0
        )
        {
            result += static_cast<char>(hex_value(value[i + 1]) * 16 + hex_value(value[i + 2]));
            i += 2;
        } else {
            result += value[i];
        }
    }
    return result;
}

std::map<std::string, std::string> parse_query(std::string_view query)
{
    std::map<std::string, std::string> result;
    while (!query.empty()) {
        const size_t amp          = query.find('&');
        const std::string_view kv = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if (kv.empty()) {
            continue;
        }
        const size_t eq = kv.find('=');
        if (eq == std::string_view::npos) {
            result[percent_decode(kv)] = "";
        } else {
            result[percent_decode(kv.substr(0, eq))] = percent_decode(kv.substr(eq + 1));
        }
    }
    return result;
}

} // namespace

HttpResponse error_response(int status, const std::string& message)
{
    return HttpResponse{status, nlohmann::json{{"error", message}}.dump()};
}

TestServer::TestServer(uint16_t port, std::string password) :
    m_port(port),
    m_password(std::move(password)),
    m_acceptor(m_io)
{}

TestServer::~TestServer()
{
    stop();
}

void TestServer::add_route(std::string method, std::string path, Handler handler)
{
    m_routes[{std::move(method), std::move(path)}] = std::move(handler);
}

bool TestServer::start()
{
    try {
        const tcp::endpoint endpoint(asio::ip::make_address_v4("127.0.0.1"), m_port);
        m_acceptor.open(endpoint.protocol());
#ifndef _WIN32
        m_acceptor.set_option(tcp::acceptor::reuse_address(true));
#endif
        m_acceptor.bind(endpoint);
        m_acceptor.listen();
        m_port = m_acceptor.local_endpoint().port();
    } catch (const boost::system::system_error& e) {
        SPDLOG_ERROR("TestServer: cannot listen on 127.0.0.1:{}: {}", m_port, e.what());
        boost::system::error_code ec;
        m_acceptor.close(ec);
        return false;
    }

    m_thread =
        Biz::JThread::JThread([this](Biz::JThread::StopToken stop_token) { run(stop_token); });
    return true;
}

void TestServer::stop()
{
    m_stopping = true;
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
    boost::system::error_code ec;
    m_acceptor.close(ec);
}

uint16_t TestServer::port() const
{
    return m_port;
}

bool TestServer::stopping() const
{
    return m_stopping;
}

bool TestServer::run_until(
    const bool& done,
    const Biz::JThread::StopToken& stop_token,
    const std::function<void()>& cancel
)
{
    m_io.restart();
    bool cancelled = false;
    while (!done) {
        if (!cancelled && stop_token.stop_requested()) {
            cancel();
            cancelled = true;
        }
        m_io.run_for(poll_interval);
        if (m_io.stopped()) {
            m_io.restart();
        }
    }
    return !cancelled;
}

void TestServer::run(Biz::JThread::StopToken stop_token)
{
    while (!stop_token.stop_requested()) {
        tcp::socket socket(m_io);
        boost::system::error_code ec;
        bool done = false;
        m_acceptor.async_accept(
            socket,
            [&](const boost::system::error_code& error)
            {
                ec   = error;
                done = true;
            }
        );
        if (!run_until(
                done,
                stop_token,
                [this]
                {
                    boost::system::error_code cancel_ec;
                    m_acceptor.cancel(cancel_ec);
                }
            ))
        {
            break;
        }
        if (ec) {
            SPDLOG_WARN("TestServer: accept failed: {}", ec.message());
            continue;
        }
        handle_connection(std::move(socket), stop_token);
    }
}

void TestServer::handle_connection(tcp::socket socket, const Biz::JThread::StopToken& stop_token)
{
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(body_limit);

    boost::system::error_code ec;
    bool done         = false;
    const auto cancel = [&stream] { stream.cancel(); };

    stream.expires_after(connection_timeout);
    http::async_read(
        stream,
        buffer,
        parser,
        [&](const boost::system::error_code& error, size_t)
        {
            ec   = error;
            done = true;
        }
    );
    if (!run_until(done, stop_token, cancel)) {
        return;
    }

    HttpResponse response;
    unsigned version = 11;
    if (ec == http::error::body_limit) {
        response = error_response(400, "Request body too large");
    } else if (ec) {
        SPDLOG_DEBUG("TestServer: read failed: {}", ec.message());
        return;
    } else {
        const http::request<http::string_body>& message = parser.get();
        version                                         = message.version();

        HttpRequest request;
        request.method                = std::string(message.method_string());
        const std::string_view target = message.target();
        const size_t question         = target.find('?');
        request.path                  = std::string(target.substr(0, question));
        if (question != std::string_view::npos) {
            request.query = parse_query(target.substr(question + 1));
        }
        request.body = message.body();

        response = dispatch(request, std::string(message[http::field::authorization]));
    }

    http::response<http::string_body> reply{static_cast<http::status>(response.status), version};
    reply.set(http::field::server, "yogatest");
    reply.set(http::field::content_type, response.content_type);
    if (response.status == 401) {
        reply.set(http::field::www_authenticate, "Basic realm=\"yogatest\"");
    }
    reply.keep_alive(false);
    reply.body() = std::move(response.body);
    reply.prepare_payload();

    done = false;
    stream.expires_after(connection_timeout);
    http::async_write(
        stream,
        reply,
        [&](const boost::system::error_code& error, size_t)
        {
            ec   = error;
            done = true;
        }
    );
    if (!run_until(done, stop_token, cancel)) {
        return;
    }

    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

HttpResponse
TestServer::dispatch(const HttpRequest& request, const std::string& authorization) const
{
    if (!authorized(authorization)) {
        return error_response(401, "Unauthorized");
    }

    const auto route = m_routes.find({request.method, request.path});
    if (route == m_routes.end()) {
        const bool path_known = std::ranges::
            any_of(m_routes, [&](const auto& entry) { return entry.first.second == request.path; });
        return path_known ?
            error_response(405, "Method " + request.method + " not allowed for " + request.path) :
            error_response(404, "Unknown route " + request.path);
    }

    try {
        return route->second(request);
    } catch (const std::exception& e) {
        return error_response(500, e.what());
    } catch (...) {
        return error_response(500, "Unknown error");
    }
}

bool TestServer::authorized(const std::string& authorization) const
{
    if (authorization.size() < basic.size()
        || !boost::algorithm::
               iequals(std::string_view(authorization).substr(0, basic.size()), basic))
    {
        return false;
    }

    std::string_view encoded = std::string_view(authorization).substr(basic.size());
    while (!encoded.empty() && encoded.front() == ' ') {
        encoded.remove_prefix(1);
    }
    while (!encoded.empty() && encoded.back() == ' ') {
        encoded.remove_suffix(1);
    }

    std::string decoded(beast::detail::base64::decoded_size(encoded.size()), '\0');
    const auto [written, read] =
        beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
    decoded.resize(written);

    const size_t colon = decoded.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    return decoded.substr(colon + 1) == m_password;
}

} // namespace Slic3r::App::Desktop::Testing
