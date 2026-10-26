#pragma once

#include <jthread/JThread.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace Slic3r::App::Desktop::Testing {

struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::string body;
};

struct HttpResponse
{
    int status = 200;
    std::string body;
    std::string content_type = "application/json; charset=utf-8";
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;

HttpResponse error_response(int status, const std::string& message);

class TestServer
{
public:
    TestServer(uint16_t port, std::string password);
    ~TestServer();

    TestServer(const TestServer&)            = delete;
    TestServer& operator=(const TestServer&) = delete;

    void add_route(std::string method, std::string path, Handler handler);

    bool start();
    void stop();

    uint16_t port() const;
    bool stopping() const;

private:
    void run(Biz::JThread::StopToken stop_token);
    void handle_connection(
        boost::asio::ip::tcp::socket socket,
        const Biz::JThread::StopToken& stop_token
    );
    bool run_until(
        const bool& done,
        const Biz::JThread::StopToken& stop_token,
        const std::function<void()>& cancel
    );
    HttpResponse dispatch(const HttpRequest& request, const std::string& authorization) const;
    bool authorized(const std::string& authorization) const;

    uint16_t m_port;
    std::string m_password;
    std::map<std::pair<std::string, std::string>, Handler> m_routes;

    boost::asio::io_context m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    Biz::JThread::JThread m_thread;
    std::atomic<bool> m_stopping{false};
};

} // namespace Slic3r::App::Desktop::Testing
