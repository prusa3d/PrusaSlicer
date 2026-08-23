///|/ Copyright (c) Filament DB integration
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_FilamentScanClient_hpp_
#define slic3r_GUI_FilamentScanClient_hpp_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Slic3r { namespace GUI {

namespace filament_scan_detail {

/**
 * One parsed SSE record. `event_type` defaults to "message" per the
 * EventSource spec; `data` is the concatenation of every `data:` line
 * in the record.
 */
struct ParsedEvent {
    std::string event_type = "message";
    std::string data;
};

/**
 * Stateful incremental SSE parser. The libcurl write callback feeds
 * raw bytes in arbitrary-sized chunks; the parser normalises line
 * endings (RFC EventSource allows CR, LF, or CRLF — codex flagged
 * that the original LF-only split silently dropped events from
 * standards-compliant CRLF publishers), splits on the blank-line
 * record separator, and yields each complete record via `emit`.
 *
 * Exposed for unit testing in tests/slic3rutils/.
 */
class SseRecordParser {
public:
    using Emit = std::function<void(const ParsedEvent&)>;

    SseRecordParser() = default;

    /// Append `n` bytes from `chunk` to the buffer and emit every
    /// complete record found, in order. Safe to call across chunk
    /// boundaries: a CRLF straddling two chunks is correctly handled.
    void feed(const char* chunk, std::size_t n, const Emit& emit);

    /// Test helpers.
    const std::string& buffer() const { return m_buffer; }
    bool               last_was_cr() const { return m_last_was_cr; }

private:
    std::string m_buffer;
    /// Track CR-at-end-of-chunk so a CRLF straddling chunks doesn't
    /// produce two LFs in the normalised buffer.
    bool m_last_was_cr = false;
};

/**
 * Parse a single SSE record body (newline-delimited lines, with the
 * record-terminating blank line already stripped) into a `ParsedEvent`.
 * Public for unit testing — `SseRecordParser::feed` calls into this
 * for every complete record.
 */
ParsedEvent parse_record(const std::string& record);

} // namespace filament_scan_detail


// Subscribes to a Filament DB instance's `/api/scan/stream` Server-Sent
// Events endpoint and switches the active filament preset to match each
// scanned NFC tag. Runs a libcurl loop on a worker thread; every event
// is marshalled back to the wx GUI thread before touching any preset
// state.
//
// Lifecycle: construct in `GUI_App::post_init()` after `preset_bundle`
// is loaded, destroy from `~GUI_App`. `start()` is safe to call from
// the GUI thread and is idempotent; `stop()` blocks until the worker
// has joined.
//
// The base URL points at the Filament DB instance (e.g.
// `http://localhost:3456` for a same-machine Electron deploy or
// `http://raspberrypi.local:3456` for a Pi running headlessly). The
// slicer does not need to be on the same physical machine — only
// network-reachable to the Filament DB instance.
class FilamentScanClient
{
public:
    explicit FilamentScanClient(std::string base_url);
    ~FilamentScanClient();

    FilamentScanClient(const FilamentScanClient&)            = delete;
    FilamentScanClient& operator=(const FilamentScanClient&) = delete;

    void start();
    void stop();

    /// Normalised base URL (trailing slashes stripped). Exposed for tests
    /// of the normalization logic; production callers don't need it.
    const std::string& base_url() const { return m_base_url; }

    // Public so the libcurl write callback (translation-unit-local in the
    // .cpp file) can dispatch each parsed SSE record back into the class
    // without needing friend declarations.
    void handle_event(const std::string& event_type, const std::string& data_json);

private:
    void run();

    std::string             m_base_url;
    std::atomic<bool>       m_stop { false };
    std::atomic<bool>       m_running { false };
    std::thread             m_thread;
    // Pair used to interrupt the reconnect-loop sleep when stop() is
    // called — otherwise shutdown can block for up to 30 s waiting on
    // the next backoff tick (codex P1 on PR #13).
    std::mutex              m_stop_mtx;
    std::condition_variable m_stop_cv;
};

} } // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentScanClient_hpp_
