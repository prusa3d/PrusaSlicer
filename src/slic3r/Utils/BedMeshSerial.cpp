///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedMeshSerial.hpp"
#include "Serial.hpp"

#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string_view>
#include <thread>

namespace Slic3r {
namespace Utils {

namespace asio = boost::asio;
using boost::system::error_code;
using Slic3r::GUI::BedMeshData;

// Case-insensitive substring search. Returns the index of the first occurrence
// of `needle` in `hay`, or npos if none. Empty needle matches at 0.
static std::size_t ci_find(std::string_view hay, std::string_view needle)
{
    if (needle.empty()) return 0;
    if (hay.size() < needle.size()) return std::string::npos;
    auto to_lower = [](unsigned char c) { return static_cast<unsigned char>(std::tolower(c)); };
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (to_lower(hay[i + j]) != to_lower(needle[j])) { match = false; break; }
        }
        if (match) return i;
    }
    return std::string::npos;
}

// Human-readable wrapper for serial exceptions: translates common platform
// errors ("Permission denied", "Device or resource busy", "No such file")
// into user-actionable messages. Falls back to the raw what() otherwise.
static std::string explain_serial_error(const std::string& port, const std::string& what)
{
    std::string lower = what;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower.find("permission denied") != std::string::npos ||
        lower.find("access is denied")  != std::string::npos)
        return "Port " + port + " is in use by another application "
               "(OctoPrint, Pronterface, PrusaConnect, or another slicer).";
    if (lower.find("resource busy")     != std::string::npos ||
        lower.find("device or resource busy") != std::string::npos)
        return "Port " + port + " is busy. Close any other application that "
               "might be connected to the printer and try again.";
    if (lower.find("no such file")      != std::string::npos ||
        lower.find("no such device")    != std::string::npos)
        return "Port " + port + " disappeared. Is the printer still plugged in?";
    return "Serial error on " + port + ": " + what;
}

// Read from the serial port until we see a line equal to "ok" or the deadline
// expires. Returns the accumulated response split into lines (not including
// the terminal "ok").
static bool read_until_ok(Serial& serial,
                          asio::io_context& io,
                          std::vector<std::string>& out_lines,
                          unsigned timeout_ms,
                          std::string& error)
{
    asio::deadline_timer timer(io);
    std::string buffer;
    bool        saw_ok      = false;
    bool        timed_out   = false;
    char        ch          = 0;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (!saw_ok && !timed_out) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            timed_out = true;
            break;
        }

        io.restart();
        bool read_done = false;
        error_code  read_ec;

        asio::async_read(serial, asio::buffer(&ch, 1),
            [&](const error_code& ec, size_t) {
                read_ec   = ec;
                read_done = true;
                timer.cancel();
            });

        timer.expires_from_now(boost::posix_time::milliseconds(remaining));
        timer.async_wait([&](const error_code& ec) {
            if (!ec) { // fired, not cancelled
                timed_out = true;
                serial.cancel();
            }
        });

        io.run();

        if (!read_done || read_ec || timed_out)
            break;

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            if (buffer == "ok")
                saw_ok = true;
            else if (!buffer.empty())
                out_lines.push_back(std::move(buffer));
            buffer.clear();
        } else {
            buffer.push_back(ch);
        }
    }

    if (!saw_ok) {
        if (!buffer.empty())
            out_lines.push_back(std::move(buffer));
        error = timed_out ? "Timed out waiting for 'ok' from printer"
                          : "Serial read failed before seeing 'ok'";
        return false;
    }
    return true;
}

// Send a command terminated by '\n' and read response until "ok".
static bool send_and_wait(Serial& serial,
                          asio::io_context& io,
                          const std::string& cmd,
                          std::vector<std::string>& out_lines,
                          unsigned timeout_ms,
                          std::string& error)
{
    const std::string full = cmd + "\n";
    error_code ec;
    asio::write(serial, asio::buffer(full), ec);
    if (ec) {
        error = "Serial write failed: " + ec.message();
        return false;
    }
    return read_until_ok(serial, io, out_lines, timeout_ms, error);
}

// Pure-function helper: inspect a list of response lines and return the
// expected probe count. Exposed in BedMeshSerial.hpp for unit tests.
int expected_probe_count_from_m115_lines(const std::vector<std::string>& lines)
{
    constexpr std::string_view kPrefix = "MACHINE_TYPE:";
    for (const auto& line : lines) {
        auto pos = ci_find(line, kPrefix);
        if (pos == std::string::npos)
            continue;
        std::string_view tail(line);
        tail.remove_prefix(pos + kPrefix.size());
        auto contains = [&](std::string_view needle) {
            return ci_find(tail, needle) != std::string::npos;
        };
        // XL:  12×12 grid = 144 (Configuration_XL.h)
        if (contains("XL"))
            return 144;
        // iX:   9×9 grid  =  81 (Configuration_iX.h)
        if (contains("iX"))
            return 81;
        // MINI: 4×4 grid  =  16 (Configuration_MINI.h)
        if (contains("MINI"))
            return 16;
        // Core One / MK4 / MK4S / MK3.5: 7×7 grid = 49
        if (contains("Core-One") || contains("CoreOne") ||
            contains("MK4")      || contains("MK3.5"))
            return 49;
        break;
    }
    return 0; // unknown model → pulse-mode progress
}

// Pure-function helper: parse a firmware line for an M73 progress report.
// Accepts optional "echo:" / "// " prefixes and whitespace around the P value.
int parse_m73_progress(const std::string& line)
{
    std::string_view s(line);
    // Strip common leading junk.
    auto strip_ws = [&]() {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                              s.front() == '\r' || s.front() == '\n'))
            s.remove_prefix(1);
    };
    strip_ws();
    if (s.substr(0, 5) == "echo:") s.remove_prefix(5);
    else if (s.substr(0, 3) == "// ") s.remove_prefix(3);
    else if (s.substr(0, 2) == "//") s.remove_prefix(2);
    strip_ws();
    // Must start with M73 (case-insensitive) followed by a separator.
    if (s.size() < 4) return -1;
    auto lc = [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); };
    if (lc(s[0]) != 'm' || s[1] != '7' || s[2] != '3') return -1;
    if (s[3] != ' ' && s[3] != '\t') return -1;
    s.remove_prefix(4);
    // Find " P" (the percent field).
    auto p_pos = ci_find(s, "P");
    if (p_pos == std::string::npos) return -1;
    s.remove_prefix(p_pos + 1);
    strip_ws();
    int pct = 0;
    bool any = false;
    while (!s.empty() && s.front() >= '0' && s.front() <= '9') {
        pct = pct * 10 + (s.front() - '0');
        s.remove_prefix(1);
        any = true;
    }
    if (!any) return -1;
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    return pct;
}

G29ProgressTracker::Update G29ProgressTracker::observe(const std::string& line)
{
    Update out;

    // 1) M73 progress — percentage-accurate, model-agnostic.
    const int pct = parse_m73_progress(line);
    if (pct >= 0) {
        if (pct != m_last_m73) {
            m_last_m73 = pct;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", pct);
            out.emit  = true;
            out.step  = pct;
            out.total = 100;
            out.label = buf;
        }
        return out;
    }

    // Denominator: expected_probes if known and not overflowed, else pulse.
    auto effective_total = [&]() {
        return (m_expected > 0 && m_probes_ok <= m_expected) ? m_expected : 0;
    };

    // 2) Probe count fallback. Suppressed once M73 has ever fired to avoid
    // clobbering the percentage with point-count strings.
    if (line.find("Probe classified as clean and OK") != std::string::npos) {
        ++m_probes_ok;
        if (m_last_m73 >= 0)
            return out;
        const int total = effective_total();
        char buf[48];
        if (total > 0)
            std::snprintf(buf, sizeof(buf), "Point %d of %d", m_probes_ok, total);
        else
            std::snprintf(buf, sizeof(buf), "Point %d", m_probes_ok);
        out.emit  = true;
        out.step  = m_probes_ok;
        out.total = total;
        out.label = buf;
        return out;
    }

    // 3) Soft status messages — keep the bar alive at its current progress.
    if (line.find("Extrapolating") != std::string::npos) {
        out.emit  = true;
        out.label = "Extrapolating mesh…";
        out.step  = m_last_m73 >= 0 ? m_last_m73 : m_probes_ok;
        out.total = m_last_m73 >= 0 ? 100        : effective_total();
    } else if (line.find("Insufficient") != std::string::npos) {
        out.emit  = true;
        out.label = "Insufficient probe data";
        out.step  = m_last_m73 >= 0 ? m_last_m73 : m_probes_ok;
        out.total = m_last_m73 >= 0 ? 100        : effective_total();
    }
    return out;
}

// Pure-function helper: physical toolheads described by a printer profile.
// An MMU multiplexes filament slots into one hot end, so its per-slot
// nozzle_diameter entries are not tools. See the header.
int physical_tool_count(int nozzle_diameter_count, bool single_extruder_multi_material)
{
    if (single_extruder_multi_material)
        return 1;
    return nozzle_diameter_count > 0 ? nozzle_diameter_count : 1;
}

// Pure-function helper: which tool to pick up between G28 and G29. See the
// header for the rationale; the -1 case is load-bearing for printer safety.
int resolve_probe_tool(int extruder_count, int selected_tool, bool probe_all_tools)
{
    if (extruder_count <= 1)
        return -1;
    if (probe_all_tools)
        return 0;
    if (selected_tool < 0)
        return 0;
    return (selected_tool >= extruder_count) ? extruder_count - 1 : selected_tool;
}

// Pure-function helper: parse EXTRUDER_COUNT:N out of M115 output.
int extruder_count_from_m115_lines(const std::vector<std::string>& lines)
{
    constexpr std::string_view kPrefix = "EXTRUDER_COUNT:";
    for (const auto& line : lines) {
        auto pos = ci_find(line, kPrefix);
        if (pos == std::string::npos)
            continue;
        std::string_view tail(line);
        tail.remove_prefix(pos + kPrefix.size());
        // Skip whitespace, then parse an unsigned int.
        while (!tail.empty() && (tail.front() == ' ' || tail.front() == '\t'))
            tail.remove_prefix(1);
        int n = 0;
        bool any = false;
        while (!tail.empty() && tail.front() >= '0' && tail.front() <= '9') {
            n = n * 10 + (tail.front() - '0');
            tail.remove_prefix(1);
            any = true;
        }
        if (any && n > 0) return n;
    }
    return 0;
}

// Query M115 once and cache the raw response in `out_lines`. Returns false on
// transport error (in which case the caller should fall back to defaults).
static bool query_m115(Serial& serial, asio::io_context& io,
                       std::vector<std::string>& out_lines)
{
    std::string err;
    if (!send_and_wait(serial, io, "M115", out_lines, 3'000, err))
        return false;
    for (const auto& line : out_lines)
        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M115 << " << line;
    return true;
}

// Long-running read: streams lines, calls line_cb per non-empty line, honors
// both an overall deadline and an idle timeout (deadline since last byte),
// and polls cancel_requested between reads. Returns true iff an "ok" line
// was received.
static bool stream_until_ok(Serial& serial,
                            asio::io_context& io,
                            unsigned overall_timeout_ms,
                            unsigned idle_timeout_ms,
                            std::atomic<bool>* cancel_requested,
                            const std::function<void(const std::string&)>& line_cb,
                            std::string& error)
{
    asio::deadline_timer timer(io);
    std::string buffer;
    bool saw_ok = false;
    char ch = 0;

    const auto start  = std::chrono::steady_clock::now();
    auto last_byte_at = start;

    while (!saw_ok) {
        if (cancel_requested && cancel_requested->load()) {
            error = "Cancelled";
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const long overall_left = overall_timeout_ms -
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        const long idle_left = idle_timeout_ms -
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_byte_at).count();
        if (overall_left <= 0) { error = "Overall timeout elapsed"; return false; }
        if (idle_left    <= 0) { error = "Idle timeout (no firmware output)"; return false; }

        // Use the tighter of the two remaining budgets for this read slice, but
        // cap at 500ms so we can check cancel_requested periodically.
        const long slice_ms = std::min({ overall_left, idle_left, 500L });

        io.restart();
        bool read_done = false;
        bool timed_out = false;
        error_code  read_ec;

        asio::async_read(serial, asio::buffer(&ch, 1),
            [&](const error_code& ec, size_t) {
                read_ec = ec;
                read_done = true;
                timer.cancel();
            });
        timer.expires_from_now(boost::posix_time::milliseconds(slice_ms));
        timer.async_wait([&](const error_code& ec) {
            if (!ec) { timed_out = true; serial.cancel(); }
        });
        io.run();

        // Slice timer fired: the async_read was cancelled, so the read handler
        // will have run with operation_aborted and read_done=true / read_ec set.
        // That's expected — loop and reevaluate the overall/idle budgets.
        if (timed_out)
            continue;

        if (!read_done || read_ec) {
            error = std::string("Serial read error")
                  + (read_ec ? std::string(": ") + read_ec.message() : std::string());
            return false;
        }

        last_byte_at = std::chrono::steady_clock::now();

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            if (buffer == "ok") {
                saw_ok = true;
                break;
            }
            if (!buffer.empty() && line_cb)
                line_cb(buffer);
            buffer.clear();
        } else {
            buffer.push_back(ch);
        }
    }
    return saw_ok;
}

static std::string pick_prusa_port()
{
    auto ports = scan_serial_ports_extended();
    // Prefer a device explicitly tagged as an Original Prusa printer.
    for (const auto& p : ports)
        if (p.is_printer)
            return p.port;
    // Fallback: match on Prusa Research USB VID 0x2C99 (11417 decimal).
    for (const auto& p : ports)
        if (p.id_vendor == 0x2C99)
            return p.port;
    return {};
}

BedMeshFetchResult fetch_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const std::string& explicit_port,
                                               unsigned timeout_ms)
{
    BedMeshFetchResult result;

    std::string port = explicit_port.empty() ? pick_prusa_port() : explicit_port;
    if (port.empty()) {
        result.error = "No Prusa printer found on USB serial. "
                       "Make sure it is connected and not busy with another app.";
        return result;
    }
    result.port_used = port;

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);

        // Brief settle: some USB-CDC devices drop the first few bytes.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Drain any unsolicited output already buffered (temperature reports, etc.)
        {
            error_code ec;
            char drain_buf[256];
            // Non-blocking drain: try to read available bytes with a tight timer.
            asio::deadline_timer t(io);
            bool drained = false;
            std::size_t n = 0;
            io.restart();
            asio::async_read(serial, asio::buffer(drain_buf, sizeof(drain_buf)),
                asio::transfer_at_least(1),
                [&](const error_code&, std::size_t got) { n = got; drained = true; t.cancel(); });
            t.expires_from_now(boost::posix_time::milliseconds(100));
            t.async_wait([&](const error_code& ec) {
                if (!ec) { serial.cancel(); drained = true; }
            });
            io.run();
            (void)drained; (void)n;
        }

        std::vector<std::string> dummy;
        std::string err;

        // Enable stored bed leveling so the mesh is actually active before we dump.
        if (!send_and_wait(serial, io, "M420 S1", dummy, timeout_ms, err)) {
            result.error = "M420 S1 failed: " + err;
            return result;
        }
        dummy.clear();

        // Dump the mesh in CSV format.
        std::vector<std::string> response;
        if (!send_and_wait(serial, io, "M420 V1 T1", response, timeout_ms, err)) {
            result.error = "M420 V1 T1 failed: " + err;
            return result;
        }

        BedMeshData mesh = BedMeshData::parse_m420_output(response, probe_min, probe_max);
        if (mesh.status != BedMeshData::Status::Loaded) {
            result.error = mesh.error_message.empty()
                ? std::string("Failed to parse mesh from printer response")
                : mesh.error_message;
            return result;
        }

        result.mesh = std::move(mesh);
        return result;
    } catch (const std::exception& e) {
        result.error = explain_serial_error(port, e.what());
        return result;
    }
}

BedMeshFetchResult probe_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const ProbeProgressCallback& progress,
                                               const BedMeshProbeOptions& options)
{
    BedMeshFetchResult result;

    std::string port = options.explicit_port.empty() ? pick_prusa_port() : options.explicit_port;
    if (port.empty()) {
        result.error = "No Prusa printer found on USB serial.";
        return result;
    }
    result.port_used = port;

    // Combined cancel flag: OR'd from cancel_requested and force_stop_requested
    // by a small watchdog thread, so stream_until_ok can keep its simple atomic
    // API. The outer code below separately checks force_stop after a phase to
    // decide whether to send M112 and bail.
    std::atomic<bool> combined_cancel{ false };
    std::atomic<bool> watchdog_stop { false };
    std::thread watchdog([&]() {
        while (!watchdog_stop.load()) {
            if (options.cancel_requested && options.cancel_requested->load())
                combined_cancel.store(true);
            if (options.force_stop_requested && options.force_stop_requested->load())
                combined_cancel.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    struct WatchdogJoiner {
        std::atomic<bool>& stop; std::thread& t;
        ~WatchdogJoiner() { stop.store(true); if (t.joinable()) t.join(); }
    } joiner{ watchdog_stop, watchdog };

    auto is_force_stop = [&]() {
        return options.force_stop_requested && options.force_stop_requested->load();
    };

    // Strip firmware chatter prefixes ("echo:", "//", leading whitespace).
    auto clean = [](std::string s) {
        auto starts_with = [&](const char* p) {
            return s.compare(0, std::strlen(p), p) == 0;
        };
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(s.begin());
        if (starts_with("echo:")) s.erase(0, 5);
        else if (starts_with("// ")) s.erase(0, 3);
        else if (starts_with("//"))  s.erase(0, 2);
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(s.begin());
        return s;
    };

    auto emit = [&](const char* stage, const std::string& detail = {}, int step = 0, int total = 0) {
        if (progress) {
            BedMeshProbeProgress p;
            p.stage = stage;
            p.detail = clean(detail);
            p.step = step;
            p.total_steps = total;
            progress(p);
        }
    };

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Helper: fire M112 (emergency stop) on force-stop. The firmware halts
        // steppers and heaters and enters a fault state that requires a reset
        // — the nuclear option. No commands should follow this on the same
        // connection.
        auto send_emergency_stop = [&]() {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M112\n")), ec);
            BOOST_LOG_TRIVIAL(warning) << "[BedMesh probe] >> M112 (force stop)";
        };

        // Drain any unsolicited output that accumulated on the CDC buffer during
        // the USB-serial reset (temperature reports, "T:..." lines, remnants of
        // a previous aborted command). Without this, the first `stream_until_ok`
        // can see stale bytes that corrupt the read state.
        {
            error_code ec;
            char       buf[1024];
            asio::deadline_timer t(io);
            bool       done = false;
            io.restart();
            asio::async_read(serial, asio::buffer(buf, sizeof(buf)),
                asio::transfer_at_least(1),
                [&](const error_code&, std::size_t n) {
                    if (n > 0)
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] drained " << n << " bytes";
                    done = true;
                    t.cancel();
                });
            t.expires_from_now(boost::posix_time::milliseconds(250));
            t.async_wait([&](const error_code& tec) {
                if (!tec) { serial.cancel(); done = true; }
            });
            io.run();
            (void)done;
        }

        std::string err;

        // Step 0: query printer model so we can size the progress bar. Non-fatal
        // if this fails — we just fall back to pulse-mode progress.
        std::vector<std::string> m115_lines;
        (void)query_m115(serial, io, m115_lines);
        const int expected_probes = expected_probe_count_from_m115_lines(m115_lines);
        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] expected_probes=" << expected_probes;
        // 0 when M115 did not report EXTRUDER_COUNT (unknown printer).
        const int tool_count = extruder_count_from_m115_lines(m115_lines);
        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] tool_count=" << tool_count;

        // Step 1: home all axes.
        emit("Homing", "G28");
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G28\n")), ec);
            if (ec) { result.error = "Write G28 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> G28";
            if (!stream_until_ok(serial, io, /*overall*/ 180'000, /*idle*/ 60'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] G28 << " << line;
                        if (line.find("busy") == std::string::npos)
                            emit("Homing", line);
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "G28 failed: " + err;
                return result;
            }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] G28 done (ok)";
        }

        // Step 1b: pick up a tool. MUST run after G28 and before any heating or
        // probing — but ONLY on printers that actually have a tool to pick up.
        //
        // On a toolchanger (XL, INDX), G28 parks the active tool in its dock and
        // leaves the carriage EMPTY. The bed probe is triggered by the nozzle
        // itself, so a G29 issued with an empty carriage never trips: the bed keeps
        // driving upward into the toolhead until something gives. This code used to
        // assume the first pass ran with "whichever tool was active (typically T0)"
        // — an assumption that is false precisely on the printers where it does
        // damage.
        //
        // On a single-tool Nextruder printer (MK4/MK4S/MK3.9/Core One/MINI) the
        // caller leaves probe_tool at -1 and we emit nothing, so the command stream
        // is byte-for-byte what it has always been there. That is deliberate: an
        // unsolicited T0 is not a harmless no-op if an MMU is attached, where T<n>
        // selects a filament slot and would trigger a load mid-probe. The slicer
        // itself never emits T0 for a single-extruder print either
        // (GCodeWriter::toolchange only emits when multiple_extruders is set).
        //
        // Mirrors the ordering the per-tool loop below already uses: T<n> -> M109 -> G29.
        if (options.probe_tool >= 0) {
            const std::string tool = "T" + std::to_string(options.probe_tool);
            emit("Selecting tool", tool);
            error_code ec;
            asio::write(serial, asio::buffer(tool + "\n"), ec);
            if (ec) { result.error = "Write " + tool + " failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> " << tool;
            if (!stream_until_ok(serial, io, /*overall*/ 90'000, /*idle*/ 30'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] " << tool << " << " << line;
                        if (line.find("busy") == std::string::npos)
                            emit("Selecting tool", line);
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                // Never fall through to G29 here: the caller only sets probe_tool on a
                // multi-tool printer, so a failure means the carriage may be empty.
                result.error =
                    "Could not pick up " + tool + " (" + err + ").\n\n"
                    "Refusing to probe. Homing parks the tool on a toolchanger, and "
                    "running G29 with an empty toolhead drives the bed into the carriage "
                    "— the nozzle is what trips the probe.\n\n"
                    "Check the tool is seated in its dock and retry.";
                return result;
            }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] " << tool << " done (ok)";
        } else {
            BOOST_LOG_TRIVIAL(debug)
                << "[BedMesh probe] no tool selection (single-tool printer); "
                   "command stream unchanged";
        }

        // Step 2a: heat bed (optional, slow). Skipped when bed_temp_c == 0.
        // Heating the bed before probing gives a mesh that reflects actual
        // print conditions (PEI sheets flex ~0.05 mm under thermal expansion);
        // a cold-bed mesh systematically misrepresents the first layer.
        if (options.bed_temp_c > 0) {
            emit("Heating bed", std::to_string(options.bed_temp_c) + " °C target");
            // M140: set target, non-blocking. We then fire M104 in step 2b so
            // the nozzle also warms in parallel while M190 gates on the bed.
            error_code ec;
            const std::string cmd_m140 = "M140 S" + std::to_string(options.bed_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd_m140), ec);
            if (ec) { result.error = "Write M140 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> M140 S" << options.bed_temp_c;
            if (!stream_until_ok(serial, io, /*overall*/ 15'000, /*idle*/ 10'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M140 << " << line;
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "M140 failed: " + err;
                return result;
            }

            // Also kick off nozzle heating now so it proceeds in parallel.
            const std::string cmd_m104 = "M104 S" + std::to_string(options.nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd_m104), ec);
            if (ec) { result.error = "Write M104 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> M104 S" << options.nozzle_temp_c << " (parallel)";
            (void)stream_until_ok(serial, io, 10'000, 5'000, &combined_cancel, nullptr, err);

            // M190: block on bed reaching target. Can take 3-5 min from cold.
            const std::string cmd_m190 = "M190 S" + std::to_string(options.bed_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd_m190), ec);
            if (ec) { result.error = "Write M190 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> M190 S" << options.bed_temp_c;
            if (!stream_until_ok(serial, io, /*overall*/ 600'000, /*idle*/ 60'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M190 << " << line;
                        // Temperature lines look like " T:29.2 /170.0 B:58.1 /60.0 ..."
                        const auto b = line.find("B:");
                        if (b != std::string::npos) {
                            float cur = 0.f, tgt = 0.f;
                            if (std::sscanf(line.c_str() + b, "B:%f /%f", &cur, &tgt) == 2 ||
                                std::sscanf(line.c_str() + b, "B:%f/%f",  &cur, &tgt) == 2) {
                                char buf[48];
                                std::snprintf(buf, sizeof(buf), "Bed %.0f / %.0f \xc2\xb0""C", cur, tgt);
                                emit("Heating bed", buf);
                            }
                        }
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "M190 failed: " + err;
                return result;
            }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M190 done (ok)";
        }

        // Step 2b: heat nozzle (ensure target; M109 blocks until reached).
        // If bed heating ran we already kicked off M104, so this is typically
        // a quick wait; from cold it can take 30-60s.
        emit("Heating nozzle", std::to_string(options.nozzle_temp_c) + " °C target");
        if (options.bed_temp_c <= 0) {
            // No parallel pre-heat happened; send M104 now.
            error_code ec;
            const std::string cmd = "M104 S" + std::to_string(options.nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M104 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> M104 S" << options.nozzle_temp_c;
            if (!stream_until_ok(serial, io, /*overall*/ 15'000, /*idle*/ 10'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M104 << " << line;
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "M104 failed: " + err;
                return result;
            }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M104 done (ok)";
        }
        {
            error_code ec;
            const std::string cmd = "M109 S" + std::to_string(options.nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M109 failed: " + ec.message(); return result; }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> M109 S" << options.nozzle_temp_c;
            if (!stream_until_ok(serial, io, /*overall*/ 300'000, /*idle*/ 60'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M109 << " << line;
                        // Firmware temperature status lines look like:
                        //   " T:169.55/170.00 B:25.03/0.00 X:... A:... @:86 ..."
                        // Extract just the nozzle current/target.
                        const auto t = line.find("T:");
                        if (t != std::string::npos) {
                            float cur = 0.f, tgt = 0.f;
                            if (std::sscanf(line.c_str() + t, "T:%f/%f", &cur, &tgt) == 2) {
                                char buf[48];
                                std::snprintf(buf, sizeof(buf), "%.0f / %.0f \xc2\xb0""C", cur, tgt);
                                emit("Heating nozzle", buf);
                            }
                        }
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "M109 failed: " + err;
                return result;
            }
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] M109 done (ok)";
        }

        // Step 3: probe. The 7×7 printers (Core One / MK4 / MINI) complete ~49
        // points; the XL probes each heatbed tile and lands much higher.
        //
        // Progress source priority:
        //   1. `M73 Pnn` lines emitted by Buddy during G29 — percent-accurate
        //      for every printer model, even those not in the expected-count
        //      table. Rendered as "nn%" with total=100.
        //   2. Fallback: count "Probe classified as clean and OK" lines. Uses
        //      expected_probes (from M115) as the denominator; falls back to
        //      pulse mode if either unknown or if the count overflows.
        emit("Probing", "G29 (this takes ~2 minutes)", 0, expected_probes);
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G29\n")), ec);
            if (ec) { result.error = "Write G29 failed: " + ec.message(); return result; }
            G29ProgressTracker tracker(expected_probes);
            if (!stream_until_ok(serial, io, /*overall*/ 600'000, /*idle*/ 60'000,
                    &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] G29 << " << line;
                        const auto u = tracker.observe(line);
                        if (u.emit)
                            emit("Probing", u.label, u.step, u.total);
                        // Skip the noisy "busy: processing", "Re-tared", "Starting probe"
                        // lines — they show up on every point and clutter the dialog.
                    }, err)) {
                if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                result.error = "G29 failed: " + err;
                return result;
            }
        }

        // Step 4: cool nozzle (+ bed, if we heated it). Fire-and-forget: don't
        // block the user on the post-probe cool-down.
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M104 S0\n")), ec);
            stream_until_ok(serial, io, 5'000, 5'000, &combined_cancel, nullptr, err);
            if (options.bed_temp_c > 0) {
                asio::write(serial, asio::buffer(std::string("M140 S0\n")), ec);
                stream_until_ok(serial, io, 5'000, 5'000, &combined_cancel, nullptr, err);
            }
        }

        // Step 5: enable & dump mesh.
        emit("Reading mesh", "M420 V1 T1");
        std::vector<std::string> dummy, response;
        if (!send_and_wait(serial, io, "M420 S1", dummy, 5'000, err)) {
            result.error = "M420 S1 failed: " + err;
            return result;
        }
        if (!send_and_wait(serial, io, "M420 V1 T1", response, 10'000, err)) {
            result.error = "M420 V1 T1 failed: " + err;
            return result;
        }

        Slic3r::GUI::BedMeshData mesh =
            Slic3r::GUI::BedMeshData::parse_m420_output(response, probe_min, probe_max);
        if (mesh.status != Slic3r::GUI::BedMeshData::Status::Loaded) {
            result.error = mesh.error_message.empty()
                ? std::string("Failed to parse probed mesh")
                : mesh.error_message;
            return result;
        }

        // Step 6 (optional): per-tool loop for the XL. The initial pass above
        // explicitly selected T0 (step 1b), so that mesh is T0's. Now switch to
        // each other tool, re-heat its hotend to nozzle_temp_c, re-probe, and
        // read the resulting mesh. Re-uses the existing bed-temperature — we
        // do NOT cool and re-heat the bed between tools, that would double
        // the total runtime on an XL.
        if (options.probe_all_tools) {
            const int effective_tools = (tool_count > 1) ? tool_count : 1;
            BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] probe_all_tools: tool_count=" << tool_count;

            if (effective_tools > 1) {
                result.per_tool_meshes.reserve(effective_tools);
                result.per_tool_meshes.push_back(mesh); // T0 from the main pass

                for (int t = 1; t < effective_tools; ++t) {
                    if (options.cancel_requested && options.cancel_requested->load()) {
                        BOOST_LOG_TRIVIAL(info) << "[BedMesh probe] cancel before tool T" << t;
                        break;
                    }
                    emit("Tool switch", "T" + std::to_string(t), t, effective_tools);
                    // Select the new tool.
                    {
                        error_code ec;
                        const std::string cmd = "T" + std::to_string(t) + "\n";
                        asio::write(serial, asio::buffer(cmd), ec);
                        if (ec) { result.error = "Write T" + std::to_string(t) + " failed: " + ec.message(); return result; }
                        BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] >> T" << t;
                        if (!stream_until_ok(serial, io, /*overall*/ 60'000, /*idle*/ 20'000,
                                &combined_cancel,
                                [&](const std::string& line) {
                                    BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] T" << t << " << " << line;
                                }, err)) {
                            if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                            result.error = "T" + std::to_string(t) + " failed: " + err;
                            return result;
                        }
                    }
                    // Heat this tool's hotend.
                    emit("Heating nozzle", "T" + std::to_string(t) + " " + std::to_string(options.nozzle_temp_c) + " °C", t, effective_tools);
                    {
                        error_code ec;
                        const std::string cmd = "M109 S" + std::to_string(options.nozzle_temp_c) + "\n";
                        asio::write(serial, asio::buffer(cmd), ec);
                        if (ec) { result.error = "Write M109 for T" + std::to_string(t) + " failed: " + ec.message(); return result; }
                        if (!stream_until_ok(serial, io, /*overall*/ 180'000, /*idle*/ 60'000,
                                &combined_cancel, nullptr, err)) {
                            if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                            result.error = "M109 (T" + std::to_string(t) + ") failed: " + err;
                            return result;
                        }
                    }
                    // Probe again with this tool.
                    emit("Probing", "T" + std::to_string(t) + " G29", t, effective_tools);
                    {
                        error_code ec;
                        asio::write(serial, asio::buffer(std::string("G29\n")), ec);
                        if (ec) { result.error = "Write G29 (T" + std::to_string(t) + ") failed: " + ec.message(); return result; }
                        G29ProgressTracker tracker_t(expected_probes);
                        if (!stream_until_ok(serial, io, /*overall*/ 600'000, /*idle*/ 60'000,
                                &combined_cancel,
                                [&](const std::string& line) {
                                    const auto u = tracker_t.observe(line);
                                    if (u.emit) {
                                        // Prefix the label with the tool number so the
                                        // user knows which tool the bar is for.
                                        emit("Probing",
                                             "T" + std::to_string(t) + " " + u.label,
                                             u.step, u.total);
                                    }
                                }, err)) {
                            if (is_force_stop()) { send_emergency_stop(); result.error = "Force-stopped by user (printer requires reset)."; return result; }
                            result.error = "G29 (T" + std::to_string(t) + ") failed: " + err;
                            return result;
                        }
                    }
                    // Read this tool's mesh.
                    emit("Reading mesh", "T" + std::to_string(t), t, effective_tools);
                    std::vector<std::string> dummy_t, response_t;
                    if (!send_and_wait(serial, io, "M420 S1", dummy_t, 5'000, err)) {
                        result.error = "M420 S1 (T" + std::to_string(t) + ") failed: " + err;
                        return result;
                    }
                    if (!send_and_wait(serial, io, "M420 V1 T1", response_t, 10'000, err)) {
                        result.error = "M420 V1 T1 (T" + std::to_string(t) + ") failed: " + err;
                        return result;
                    }
                    Slic3r::GUI::BedMeshData tool_mesh =
                        Slic3r::GUI::BedMeshData::parse_m420_output(response_t, probe_min, probe_max);
                    if (tool_mesh.status != Slic3r::GUI::BedMeshData::Status::Loaded) {
                        result.error = "Failed to parse T" + std::to_string(t) + " mesh: " +
                                       (tool_mesh.error_message.empty() ? std::string("(no detail)") : tool_mesh.error_message);
                        return result;
                    }
                    result.per_tool_meshes.push_back(std::move(tool_mesh));
                }

                // Return to tool 0 so the printer is left in a sensible state.
                if (effective_tools > 1) {
                    error_code ec;
                    asio::write(serial, asio::buffer(std::string("T0\n")), ec);
                    std::string drop;
                    stream_until_ok(serial, io, 60'000, 20'000, &combined_cancel, nullptr, drop);
                }
            }
        }

        // Step 7: park the toolhead in a known-safe location. After G29 the
        // head sits at the last probed point — on Core One the front-left
        // corner roughly 5 mm above the sheet — uncomfortably close for the
        // user. Sequence: lift Z so the next XY move doesn't scrape, glide
        // to the bed center, full home, then lower the bed 20 mm below the
        // nozzle. The mesh data is already stored on the printer; failures
        // here are non-fatal and we still return the parsed mesh.
        {
            const double cx = 0.5 * (probe_min.x() + probe_max.x());
            const double cy = 0.5 * (probe_min.y() + probe_max.y());
            char glide_cmd[64];
            std::snprintf(glide_cmd, sizeof(glide_cmd), "G0 X%.2f Y%.2f F6000", cx, cy);

            struct ParkStep { const char* label; const char* gcode; unsigned timeout_ms; };
            const ParkStep steps[] = {
                { "Lifting head",      "G91",          5'000  }, // relative positioning
                { "Lifting head",      "G0 Z10 F600",  20'000 }, // +10 mm so the XY glide is safe
                { "Lifting head",      "G90",          5'000  }, // back to absolute
                { "Moving to center",  glide_cmd,      30'000 }, // glide @ 100 mm/s
                { "Homing",            "G28",          150'000 }, // full home
                { "Lowering bed",      "G0 Z20 F600",  20'000 }, // Z=20 absolute → bed sits 20 mm below nozzle
            };
            std::vector<std::string> drop_lines;
            std::string drop_err;
            for (const auto& s : steps) {
                emit("Parking", s.label);
                BOOST_LOG_TRIVIAL(debug) << "[BedMesh probe] park >> " << s.gcode;
                drop_lines.clear();
                drop_err.clear();
                if (!send_and_wait(serial, io, s.gcode, drop_lines, s.timeout_ms, drop_err)) {
                    BOOST_LOG_TRIVIAL(warning) << "[BedMesh probe] park step '" << s.gcode
                                               << "' did not ack: " << drop_err
                                               << " (continuing — printer may still finish)";
                    break;
                }
            }
        }

        result.mesh = std::move(mesh);
        return result;
    } catch (const std::exception& e) {
        result.error = explain_serial_error(port, e.what());
        return result;
    }
}

} // namespace Utils
} // namespace Slic3r
