#include "LocalImportServer.hpp"

#include <algorithm>
#include <cctype>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {
namespace GUI {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {

// Reduce a client-supplied name to a safe leaf filename ending in a model
// extension. The same input maps to the same output, so re-sending a part
// overwrites the same temp file (PrusaSlicer then replaces it cleanly).
std::string safe_name(const std::string& suggested)
{
    boost::filesystem::path p(suggested);
    std::string base = p.filename().string(); // strip any path components
    std::string ext  = p.extension().string();
    std::string stem = base.substr(0, base.size() - ext.size());

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string clean;
    clean.reserve(stem.size());
    for (char c : stem) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == ' ' || c == '(' || c == ')')
            clean.push_back(c);
        else
            clean.push_back('_');
    }
    // trim spaces and dots
    while (!clean.empty() && (clean.front() == '.' || clean.front() == ' '))
        clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' '))
        clean.pop_back();
    if (clean.empty())
        clean = "model";

    static const char* kModelExts[] = { ".step", ".stp", ".stl", ".obj", ".3mf", ".amf" };
    bool ok_ext = false;
    for (const char* e : kModelExts)
        if (ext == e) { ok_ext = true; break; }
    if (!ok_ext)
        ext = ".step";

    return clean + ext;
}

boost::filesystem::path write_temp(const std::string& name, const std::string& data)
{
    boost::filesystem::path dir = boost::filesystem::temp_directory_path() / "PrusaSlicer-import";
    boost::system::error_code ec;
    boost::filesystem::create_directories(dir, ec);
    boost::filesystem::path path = dir / safe_name(name);
    boost::nowide::ofstream out(path.string().c_str(), std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return path;
}

// Reads one request, routes it, writes the response, closes the socket. Kept as a
// free function so the (Asio-free) header need not declare Asio types.
void handle_connection(const LocalImportServer::Config& cfg,
                       const LocalImportServer::FileCallback& on_file,
                       tcp::socket& socket)
{
    beast::error_code ec;
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(cfg.max_body_bytes);
    http::read(socket, buffer, parser, ec);
    if (ec) {
        socket.shutdown(tcp::socket::shutdown_both, ec);
        return;
    }
    http::request<http::string_body> req = parser.release();

    const std::string origin(req[http::field::origin]);
    const bool origin_ok = !cfg.allowed_origin.empty() && origin == cfg.allowed_origin;

    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(false);
    res.set(http::field::server, "PrusaSlicer-LocalImport");

    auto set_cors = [&]() {
        if (origin_ok) {
            res.set(http::field::access_control_allow_origin, origin);
            res.set(http::field::vary, "Origin");
        }
        res.set(http::field::access_control_allow_methods, "POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, X-Prusa-Token, X-Filename");
        res.set(http::field::access_control_max_age, "600");
    };

    std::string target(req.target());
    const std::string path = target.substr(0, target.find('?'));

    auto send = [&](http::status status, const std::string& ctype, const std::string& body) {
        res.result(status);
        res.set(http::field::content_type, ctype);
        res.body() = body;
        res.prepare_payload();
        http::write(socket, res, ec);
        socket.shutdown(tcp::socket::shutdown_both, ec);
    };

    if (req.method() == http::verb::options && path == "/open") {
        set_cors();
        send(http::status::no_content, "text/plain", {});
        return;
    }
    if (req.method() == http::verb::get && path == "/ping") {
        set_cors();
        send(http::status::ok, "application/json",
             std::string("{\"app\":\"PrusaSlicer\",\"endpoint\":\"local-import\"}"));
        return;
    }
    if (req.method() == http::verb::post && path == "/open") {
        set_cors();
        if (!origin.empty() && !origin_ok) {
            send(http::status::forbidden, "text/plain", "origin not allowed");
            return;
        }
        if (!cfg.token.empty() && std::string(req["X-Prusa-Token"]) != cfg.token) {
            send(http::status::unauthorized, "text/plain", "bad token");
            return;
        }
        if (req.body().empty()) {
            send(http::status::bad_request, "text/plain", "empty body");
            return;
        }

        std::string filename(req["X-Filename"]);
        if (filename.empty())
            filename = "model.step";

        boost::filesystem::path tmp;
        try {
            tmp = write_temp(filename, req.body());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "LocalImportServer: write temp failed: " << e.what();
            send(http::status::internal_server_error, "text/plain", "could not store file");
            return;
        }

        if (on_file)
            on_file(tmp);

        BOOST_LOG_TRIVIAL(info) << "LocalImportServer: received " << tmp.filename().string()
                                << " (" << req.body().size() << " bytes)";
        send(http::status::ok, "application/json",
             std::string("{\"status\":\"ok\",\"file\":\"") + tmp.filename().string() + "\"}");
        return;
    }

    set_cors();
    send(http::status::not_found, "text/plain", "not found");
}

} // namespace

struct LocalImportServer::Impl
{
    asio::io_context        ioc;
    std::unique_ptr<tcp::acceptor> acceptor;
};

LocalImportServer::LocalImportServer(Config cfg, FileCallback on_file)
    : m_cfg(std::move(cfg))
    , m_on_file(std::move(on_file))
    , m_impl(std::make_unique<Impl>())
{}

LocalImportServer::~LocalImportServer()
{
    stop();
}

bool LocalImportServer::start()
{
    if (m_running.load())
        return true;

    boost::system::error_code ec;
    tcp::endpoint endpoint(asio::ip::make_address(m_cfg.bind_address, ec), m_cfg.port);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "LocalImportServer: bad bind address: " << ec.message();
        return false;
    }

    m_impl->acceptor = std::make_unique<tcp::acceptor>(m_impl->ioc);
    m_impl->acceptor->open(endpoint.protocol(), ec);
    if (!ec) m_impl->acceptor->set_option(asio::socket_base::reuse_address(true), ec);
    if (!ec) m_impl->acceptor->bind(endpoint, ec);
    if (!ec) m_impl->acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "LocalImportServer: cannot listen on "
                                 << m_cfg.bind_address << ':' << m_cfg.port << " - " << ec.message();
        if (m_cfg.bind_address != "127.0.0.1" && m_cfg.bind_address != "localhost")
            BOOST_LOG_TRIVIAL(error)
                << "LocalImportServer: binding to a non-default loopback address requires it to be "
                   "assigned to the loopback interface first (e.g. macOS: 'sudo ifconfig lo0 alias "
                << m_cfg.bind_address << "').";
        m_impl->acceptor.reset();
        return false;
    }

    m_running.store(true);
    m_thread = std::thread([this] { run(); });
    BOOST_LOG_TRIVIAL(info) << "LocalImportServer: listening on http://"
                            << m_cfg.bind_address << ':' << m_cfg.port;
    return true;
}

void LocalImportServer::stop()
{
    if (!m_running.exchange(false))
        return;
    // Wake the io_context and close the acceptor from within its own thread.
    asio::post(m_impl->ioc, [this] {
        boost::system::error_code ec;
        if (m_impl->acceptor)
            m_impl->acceptor->close(ec);
    });
    m_impl->ioc.stop();
    if (m_thread.joinable())
        m_thread.join();
    m_impl->ioc.restart();
    m_impl->acceptor.reset();
}

void LocalImportServer::run()
{
    // Sequential accept/handle loop. Traffic is occasional (a user clicking
    // "send"), so a single-threaded synchronous handler is plenty and easy to
    // reason about.
    std::function<void()> do_accept = [&]() {
        auto socket = std::make_shared<tcp::socket>(m_impl->ioc);
        m_impl->acceptor->async_accept(*socket, [this, socket, &do_accept](const boost::system::error_code& ec) {
            if (ec) {
                if (m_running.load())
                    BOOST_LOG_TRIVIAL(trace) << "LocalImportServer: accept: " << ec.message();
                return;
            }
            handle_connection(m_cfg, m_on_file, *socket);
            if (m_running.load())
                do_accept();
        });
    };
    do_accept();
    m_impl->ioc.run();
}

} // namespace GUI
} // namespace Slic3r
