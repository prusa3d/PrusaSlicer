#ifndef slic3r_LocalImportServer_hpp_
#define slic3r_LocalImportServer_hpp_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <boost/filesystem/path.hpp>

namespace Slic3r {
namespace GUI {

// A small, optional, loopback-only HTTP server that lets an external tool POST a
// model file (STEP/STL/3MF/OBJ/...) to be opened in the running PrusaSlicer
// instance. It is intentionally generic and GUI-agnostic: it writes the received
// bytes to a temporary file and invokes a caller-supplied callback with the path.
// The caller is responsible for marshalling onto the GUI thread (e.g. via
// wxGetApp().CallAfter()) and calling Plater::load_files().
//
// Plain HTTP is sufficient: the browser code that calls this endpoint is served
// from a real HTTPS origin, and browsers permit an HTTPS page to call
// http://127.0.0.1 because loopback is a "potentially trustworthy" origin.
class LocalImportServer
{
public:
    struct Config
    {
        std::string bind_address = "127.0.0.1"; // loopback only; never 0.0.0.0
        uint16_t    port         = 8126;
        // Exact origin allowed for cross-origin (browser) calls. Empty disables
        // the CORS header entirely (no cross-origin browser access). Never "*".
        std::string allowed_origin;
        // Optional shared secret; if non-empty, requests must send a matching
        // "X-Prusa-Token" header.
        std::string token;
        // Maximum accepted request body size.
        std::size_t max_body_bytes = 256u * 1024u * 1024u;
    };

    // Called from the server thread with the path of a freshly-written temp file.
    using FileCallback = std::function<void(boost::filesystem::path)>;

    LocalImportServer(Config cfg, FileCallback on_file);
    ~LocalImportServer();

    LocalImportServer(const LocalImportServer&) = delete;
    LocalImportServer& operator=(const LocalImportServer&) = delete;

    // Starts listening. Returns false if the socket could not be bound
    // (e.g. the port is already in use).
    bool start();
    // Stops listening and joins the worker thread. Safe to call multiple times.
    void stop();

    bool running() const { return m_running.load(); }
    uint16_t port() const { return m_cfg.port; }

    // Generates a random hex access key (used for the X-Prusa-Token gate).
    static std::string generate_token();

private:
    void run();

    Config            m_cfg;
    FileCallback      m_on_file;
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };

    struct Impl; // hides the Asio/Beast types from the header
    std::unique_ptr<Impl> m_impl;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_LocalImportServer_hpp_
