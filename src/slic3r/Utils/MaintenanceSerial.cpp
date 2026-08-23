///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MaintenanceSerial.hpp"
#include "Serial.hpp"

#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

// The cold-pull recipe below was verified line-by-line against
// Prusa-Firmware-Buddy v6.6.3 and v6.9.0 (COREONE_INDX):
//  - Heating requires a picked/latched tool (base_hotend.cpp:50) → T<n> first.
//  - G1 E below EXTRUDE_MINTEMP 170 is silently stripped unless M302 S0
//    (planner.cpp:2280).
//  - M0 = QuickPause dialog on the printer LCD with a custom message; it holds
//    only the gcode queue, tool stays picked+heated (marlin_stubs/M0.cpp).
//    Messages: whole line ≤95 chars (MAX_CMD_SIZE 96), first token a plain
//    word, no semicolons (parser string_arg fallback).
//  - M109 R regulates AT the target and can exit early when the cooling slope
//    flattens (<1.5 °C/min) → follow with M104 S0 + fixed dwell.
//  - The pull retract cannot unlock the nozzle away from the dock; the lock is
//    mechanical and dock-gated (toolchanger_indx.cpp:525-575).
//  - Firmware E current default is 550 mA; 650 is what the firmware itself
//    uses for lock moves, safe for the stiff-plug pull (M906).
//  - v6.9.0 removed the Auto Retract switch on INDX (0dfee5ef1): the toggle
//    moved behind HAS_SWITCHABLE_AUTO_RETRACT, which does not list either INDX
//    variant. auto_retract.cpp:113 still returns early for filaments marked
//    is_flexible, and FLEX is the only such preset (filament_presets.cpp:192),
//    so M865 S"FLEX" L<n> is what suppresses it -> see the AUTO RETRACT note in
//    run_cold_pull() and the header of the generated file.

namespace Slic3r {
namespace Utils {

namespace asio = boost::asio;
using boost::system::error_code;

// ---------------------------------------------------------------------------
// Transport helpers. These mirror the file-static helpers in BedMeshSerial.cpp
// (deliberately self-contained, same as that feature, so neither depends on
// the other's internals).
// ---------------------------------------------------------------------------

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

// All plausible Prusa printer ports, best candidates first. Returning every
// candidate (rather than just the first) lets the caller probe each for an
// INDX -- with two Prusa printers attached, the first enumerated one may well
// be the MK4 the user did not mean.
static std::vector<std::string> prusa_port_candidates()
{
    auto ports = scan_serial_ports_extended();
    std::vector<std::string> out;
    for (const auto& p : ports)
        if (p.is_printer)
            out.push_back(p.port);
    for (const auto& p : ports)
        if (p.id_vendor == 0x2C99 // Prusa Research USB VID
            && std::find(out.begin(), out.end(), p.port) == out.end())
            out.push_back(p.port);
    return out;
}

static std::string pick_prusa_port()
{
    const auto candidates = prusa_port_candidates();
    return candidates.empty() ? std::string{} : candidates.front();
}

// Long-running read: streams lines, calls line_cb per non-empty line, honors
// an overall deadline, an idle timeout (since last byte), and a cancel flag.
// Returns true iff an "ok" line was received. Identical in behaviour to
// BedMeshSerial's stream_until_ok.
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

// Drain unsolicited output buffered on the CDC endpoint (temperature reports,
// remnants of an aborted command) so the first stream doesn't see stale bytes.
static void drain_serial(Serial& serial, asio::io_context& io)
{
    char buf[1024];
    asio::deadline_timer t(io);
    bool done = false;
    io.restart();
    asio::async_read(serial, asio::buffer(buf, sizeof(buf)),
        asio::transfer_at_least(1),
        [&](const error_code&, std::size_t n) {
            if (n > 0)
                BOOST_LOG_TRIVIAL(debug) << "[Maintenance] drained " << n << " bytes";
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

std::string detect_printer_port()
{
    return pick_prusa_port();
}

// Extract the MACHINE_TYPE value from M115 output, or "" if absent.
// Buddy emits "... MACHINE_TYPE:Prusa-<id_str> EXTRUDER_COUNT:n UUID:..."
// (src/marlin_stubs/host/M115.cpp).
static std::string machine_type_from_m115(const std::vector<std::string>& lines)
{
    constexpr std::string_view kKey = "MACHINE_TYPE:";
    for (const auto& line : lines) {
        const auto pos = line.find(kKey);
        if (pos == std::string::npos)
            continue;
        std::string tail = line.substr(pos + kKey.size());
        // Value runs to the next space (the following key is EXTRUDER_COUNT).
        const auto end = tail.find(' ');
        if (end != std::string::npos)
            tail = tail.substr(0, end);
        return tail;
    }
    return {};
}

// True when the reported machine type is an INDX variant. The id strings are
// "COREONEINDX" and "COREONEL-INDX" (include/common/printer_model_data.hpp),
// so a case-insensitive "INDX" substring covers both and any future variant.
static bool machine_type_is_indx(const std::string& machine_type)
{
    std::string upper = machine_type;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper.find("INDX") != std::string::npos;
}

// ---------------------------------------------------------------------------
// Cold pull as a standalone G-code file (print-job delivery)
// ---------------------------------------------------------------------------

std::string generate_cold_pull_gcode(const ColdPullOptions& options)
{
    const std::string tool  = std::to_string(options.tool);
    const std::string flush = std::to_string(options.flush_temp_c);
    const std::string pull  = std::to_string(options.pull_temp_c);
    // Nozzle number as printed on the machine, for human-facing text only.
    const std::string nozzle = std::to_string(options.tool + 1);
    // Two-stage warm-up stops short of the target so the PID does not overshoot.
    const std::string pull_minus_3 = std::to_string(options.pull_temp_c - 3);
    const std::string pull_minus_4 = std::to_string(options.pull_temp_c - 4);

    std::ostringstream g;

    g << "; ============================================================\n"
      << "; INDX cold pull - guided, with on-screen prompts\n"
      << "; Generated by PrusaSlicer. Nozzle " << nozzle
      << ", flush " << flush << "C, pull " << pull << "C\n"
      << "; ------------------------------------------------------------\n"
      << "; BEFORE RUNNING, at the printer:\n"
      << ";   - Settings: turn the Filament Sensor OFF\n"
      << ";   - Remove the PTFE tube from this tool\n"
      << ";   - Have light-colored PLA ready and stay at the printer.\n"
      << ";     Do NOT preload it - a prompt says when to insert it.\n"
      << ";   - Note what this nozzle's filament type is set to right now.\n"
      << ";     You need it to undo the change described below.\n"
      << "; ------------------------------------------------------------\n"
      << "; AUTO RETRACT: there is no longer a switch for it on INDX.\n"
      << "; Firmware 6.9.0 moved the toggle behind HAS_SWITCHABLE_AUTO_RETRACT,\n"
      << "; which lists COREONE, COREONEL, MK4, iX and XL but neither INDX\n"
      << "; variant, so the menu item and the stored setting are both compiled\n"
      << "; out. The one lever left is the filament type: auto retract returns\n"
      << "; early for any filament marked flexible, and FLEX is the only preset\n"
      << "; that is. So the M865 line below marks this tool FLEX for the run.\n"
      << "; This file does NOT put it back, deliberately: the end-of-print\n"
      << "; sequence runs after the last line, and with the tool back on PLA\n"
      << "; that sequence would auto-retract - reheating the nozzle to 215C,\n"
      << "; overriding the warm-wipe temperature this file sets, and ramming\n"
      << "; the melt zone you just cleared. Put it back once the print has\n"
      << "; FINISHED: send M865 S\"<TYPE>\" L" << tool << " with the type you noted\n"
      << "; above, or set it from the printer's Filament menu. Do NOT assume\n"
      << "; it was PLA - sending PLA to a nozzle that was set to PETG or ASA\n"
      << "; overwrites that. The write is PERSISTENT; a power cycle does not\n"
      << "; undo it.\n"
      << "; ------------------------------------------------------------\n"
      << "; The procedure stops at six prompts and waits for a knob\n"
      << "; press. To bail out, press the knob then Stop the print.\n"
      << "; If you Stop at the tip check, the restore block never runs -\n"
      << "; afterwards send M302 S170 and M591 R, or power-cycle, to put\n"
      << "; back the cold-extrusion guard and stuck-filament detection.\n"
      << "; The filament type is not covered by a power cycle either, because\n"
      << "; it lives in the printer's settings: send M865 S\"<TYPE>\" L" << tool << " with\n"
      << "; the type you noted, or set it back from the Filament menu.\n"
      << "; A prompt left ~30 min turns the heaters off (safety timer); the\n"
      << "; procedure re-asserts temperature after each prompt to cover that.\n"
      << "; ------------------------------------------------------------\n"
      << "; PRINTER MODEL CHECK: the M862.3 line below makes the printer\n"
      << "; refuse this file unless it is a CORE One INDX. The sequence sends\n"
      << "; toolchanger picks, high nozzle temperatures and INDX motor\n"
      << "; currents, which are wrong and potentially damaging on an MK4, XL\n"
      << "; or MINI. Do not delete the line.\n"
      << "; ============================================================\n\n";

    // Standard Prusa model gate: on a mismatch the firmware sets
    // Check::printer_model and print preview refuses the job
    // (src/common/gcode/gcode_info.cpp:273-292).
    g << "M862.3 P\"COREONEINDX\"   ; refuse to run on a non-INDX printer\n\n";

    g << "M117 Cold pull - guided\n"
      << "M0 Setup check: filament sensor OFF, PTFE tube removed. Press to continue\n\n";

    // M865 S"FLEX" L<n> is the whole auto-retract workaround. Firmware 6.9.0
    // dropped the Auto Retract switch on INDX (0dfee5ef1, BFW-8589): the toggle
    // moved behind HAS_SWITCHABLE_AUTO_RETRACT, which lists COREONE COREONEL
    // MK4 iX XL and neither INDX variant, so the menu item, the global-disable
    // check in maybe_retract_from_nozzle() and the auto_retract_enabled
    // config-store item are all compiled out. What survives is an early return
    // for filaments marked is_flexible (BFW-6953), and FLEX is the only preset
    // with the flag. M865 cannot rewrite a preset's parameters
    // (is_customizable() is false for presets), so this changes only which type
    // the tool is recorded as holding. It is a PERSISTENT write; see the header
    // above for why this file must not undo it.
    g << "M117 Picking nozzle " << nozzle << "\n"
      << "T" << tool << " M0                 ; M0 param = ignore tool mapping\n"
      << "M865 S\"FLEX\" L" << tool << "       ; suppress auto retract - see the note above\n"
      << "M302 S0               ; allow cold extrusion for the session\n"
      << "M83                   ; relative E\n"
      << "M591 S0               ; disable stuck-filament detection\n\n";

    g << "M0 Insert light PLA into the top port until it stops at the drive gears. Do not force it\n\n";

    g << "M117 Heating to " << flush << "\n"
      << "M109 S" << flush << "\n\n";

    g << "; The briefing must come BEFORE the purge: the screen and the nozzle\n"
      << "; cannot be watched at the same time, so tell the user what to look\n"
      << "; for, let them press, and only then start extruding.\n"
      << "M0 Purge next. Watch the nozzle tip for melted PLA flowing out. Press to start\n"
      << "M109 S" << flush << "             ; re-assert - the prompt is unbounded and the\n"
      << "                      ; safety timer may have cleared the target\n"
      << "M117 Purging - watch the tip\n";
    for (int i = 0; i < 3; ++i) {
        g << "G1 E20 F120\n";
        if (i < 2) g << "G4 S2\n";
    }
    g << "\n";

    // The recovery instruction has to live IN the prompt. Someone standing at
    // the printer following this cannot see the file header, and stopping here
    // skips the M591 R / M302 S170 restore block at the end -- leaving the
    // machine with stuck-filament detection and the cold-extrusion guard off.
    // A power cycle restores both (M302 is RAM-only; M591 without P is
    // re-applied from EEPROM at boot), so it is the one action that fits.
    g << "M0 Tip check. PLA out: press knob. NOTHING out: press knob, Stop print, power cycle\n"
      << "G4 S15                ; grace window to reach Stop before packing starts\n"
      << "; If nothing extruded, the blockage is above the melt zone and a\n"
      << "; cold pull cannot reach it. Power-cycling after the Stop restores the\n"
      << "; cold-extrusion guard and stuck-filament detection that this file\n"
      << "; disabled; the restore block at the end never runs in that case.\n\n";

    g << "M117 Cooling - packing\n"
      << "M109 S" << flush << "             ; prompts are unbounded - the safety timer may have\n"
      << "                      ; cleared the target, so re-assert before packing\n"
      << "M104 S180             ; fall toward 180, packing pushes on the way\n";
    for (int i = 0; i < 8; ++i) {
        g << "G1 E2 F60\n";
        if (i < 7) g << "G4 S5\n";
    }
    g << "\n";

    g << "M117 Deep cooling to 60\n"
      << "M104 S0\n"
      << "M106 S255             ; fan full\n"
      << "M109 R60              ; regulates AT 60; may exit early on slope-break\n"
      << "M104 S0               ; heater truly off\n"
      << "G4 S120               ; dwell covers an early exit above 60\n"
      << "M107\n\n";

    g << "M117 Reheating to " << pull << "\n"
      << "; Two-stage warm-up. Aimed straight at the target, the PID (not tuned\n"
      << "; for temperatures this low) overshoots, then waits ~30s to settle\n"
      << "; back down. Stop the first stage just short so the thermal momentum\n"
      << "; bleeds off, then close the last degrees under control. M109 C waits\n"
      << "; for a temperature WITHOUT changing the target (Buddy 6.6.2+; older\n"
      << "; firmware ignores it and just heats and waits as before).\n"
      << "M104 S" << pull_minus_3 << "\n"
      << "M109 C" << pull_minus_4 << "\n"
      << "M109 S" << pull << "\n\n";

    g << "M0 Pull ready at " << pull
      << "C. Motor will yank the plug - keep hands clear. Press to pull\n\n";

    g << "M117 PULLING\n"
      << "; Re-assert before committing: the prompt above is unbounded, and\n"
      << "; yanking at high current against a plug that has cooled back to\n"
      << "; room temperature can snap it or strain the drive. Two stages for\n"
      << "; the same overshoot reason as above; at temperature already, all\n"
      << "; three return immediately.\n"
      << "M104 S" << pull_minus_3 << "\n"
      << "M109 C" << pull_minus_4 << "\n"
      << "M109 S" << pull << "\n"
      << "M906 E650             ; E current up for the stiff plug\n"
      << "G1 E-40 F3000         ; 50mm/s yank\n"
      << "G1 E-40 F1200         ; feed the strand up out of the gear\n"
      << "M906 E550             ; restore INDX default current\n"
      << "M591 R                ; restore stuck-filament detection\n"
      << "M302 S170             ; restore cold-extrusion guard\n"
      << "M104 S0\n\n";

    g << "M0 Pull done. Remove strand from top port. Pressing knob warms nozzle, then wipe and dock\n\n";

    g << "; Rewarm before the end-of-print wipe. The pull left the nozzle near\n"
      << "; " << pull << "C and the unbounded prompt above lets it fall to ambient, where\n"
      << "; the wipe just drags solid strings around the printer. 170 softens\n"
      << "; PLA residue so the wiper takes it off; the heater then goes straight\n"
      << "; off and residual heat carries the few seconds of wipe and dock.\n"
      << "M117 Warming for the wipe\n"
      << "M109 S170\n"
      << "M104 S0               ; off before the teardown - nothing relies on the\n"
      << "                      ; firmware to switch the heater off afterwards\n\n";

    g << "M117 Finishing - hands off\n"
      << "; The end-of-print sequence runs automatically after the last line:\n"
      << "; nozzle wipe, tool dock, steppers off. Wait for Finished.\n"
      << "; Inspect the tip: three thin strands + debris = a good pull.\n"
      << "; Repeat until the tip comes out clean. Then, at the printer:\n"
      << "; re-enable the Filament Sensor, refit the PTFE tube, and set this\n"
      << "; tool's filament type back to what it was (M865 S\"<TYPE>\" L" << tool << ", or\n"
      << "; the Filament menu). Nothing else puts that one back for you.\n";

    return g.str();
}

// ---------------------------------------------------------------------------
// Cold pull over serial
// ---------------------------------------------------------------------------

MaintenanceResult run_cold_pull(const MaintenanceProgressCallback& progress,
                                const ColdPullOptions& options)
{
    MaintenanceResult result;

    // Find the INDX among however many Prusa printers are attached. Probing
    // only the first enumerated port would abort with "not an INDX" whenever
    // an MK4 happens to enumerate ahead of the INDX the user meant, and there
    // is no port picker in the UI to work around that.
    //
    // An explicit port is honoured as-is (still model-checked below) so the
    // PRUSASLICER_MAINTENANCE_PORT override stays authoritative.
    std::vector<std::string> candidates;
    if (!options.explicit_port.empty())
        candidates.push_back(options.explicit_port);
    else
        candidates = prusa_port_candidates();

    if (candidates.empty()) {
        result.error = "No Prusa printer found on USB serial. "
                       "Make sure it is connected and not busy with another app.";
        return result;
    }

    std::string port;
    std::vector<std::string> rejected; // "port (MODEL)" for the error message
    for (const auto& candidate : candidates) {
        try {
            asio::io_context probe_io;
            Serial probe(probe_io, candidate, 115200);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            drain_serial(probe, probe_io);

            std::vector<std::string> lines;
            error_code ec;
            asio::write(probe, asio::buffer(std::string("M115\n")), ec);
            if (ec) {
                rejected.push_back(candidate + " (no response)");
                continue;
            }
            std::string probe_err;
            if (!stream_until_ok(probe, probe_io, 10'000, 10'000, nullptr,
                    [&](const std::string& line) { lines.push_back(line); }, probe_err)) {
                rejected.push_back(candidate + " (no response)");
                continue;
            }
            const std::string machine_type = machine_type_from_m115(lines);
            if (machine_type_is_indx(machine_type)) {
                port = candidate;
                BOOST_LOG_TRIVIAL(info) << "[Maintenance] INDX found on " << candidate
                                        << " (" << machine_type << ")";
                break;
            }
            rejected.push_back(candidate + " ("
                               + (machine_type.empty() ? "unknown model" : machine_type) + ")");
        } catch (const std::exception& e) {
            rejected.push_back(candidate + " (" + e.what() + ")");
        }
    }

    if (port.empty()) {
        std::string detail;
        for (const auto& r : rejected)
            detail += (detail.empty() ? "" : ", ") + r;
        result.error = "No INDX printer found on USB serial. This procedure sends "
                       "INDX-specific tool-change, temperature and motor-current "
                       "commands and must not run on another model. Nothing was sent."
                     + (detail.empty() ? std::string{} : " Checked: " + detail + ".");
        return result;
    }
    result.port_used = port;

    // Combined cancel flag fed by a watchdog, same pattern as the bed probe:
    // stream_until_ok keeps its single-atomic API while we honor both flags.
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
    auto is_cancel = [&]() {
        return combined_cancel.load();
    };

    // Strip firmware chatter prefixes for display.
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

    // The procedure has a fixed step list; progress is slicer-driven (we know
    // which command we're on), with firmware temperature lines as detail.
    int step = 0;
    constexpr int kTotalSteps = 12;
    auto emit = [&](const char* stage, const std::string& detail = {}) {
        if (progress) {
            MaintenanceProgress p;
            p.stage = stage;
            p.detail = clean(detail);
            p.step = step;
            p.total_steps = kTotalSteps;
            progress(p);
        }
    };

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        drain_serial(serial, io);

        std::string err;

        auto send_emergency_stop = [&]() {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M112\n")), ec);
            // Only claim the stop if the bytes actually went out; a disconnect
            // here is exactly the case where the nozzle may still be hot and
            // the user must not be told otherwise.
            result.force_stopped = !ec;
            BOOST_LOG_TRIVIAL(warning) << "[Maintenance] >> M112 (force stop), delivered="
                                       << (!ec ? "yes" : "no");
        };

        // --- MODEL GATE. Port auto-detection matches any Prusa printer, so a
        // connected MK4/XL/MINI would otherwise receive this INDX-specific
        // sequence: toolchanger picks, 290 C targets and INDX motor currents.
        // Confirm we are talking to an INDX before sending anything else.
        emit("Connecting", "Identifying printer");
        {
            std::vector<std::string> m115_lines;
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M115\n")), ec);
            if (ec) {
                result.error = "Could not query the printer: " + ec.message();
                return result;
            }
            std::string m115_err;
            if (!stream_until_ok(serial, io, 10'000, 10'000, nullptr,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[Maintenance] M115 << " << line;
                        m115_lines.push_back(line);
                    }, m115_err)) {
                result.error = "The printer did not answer M115 (" + m115_err
                             + "). Cannot confirm it is an INDX, so nothing was sent.";
                return result;
            }
            const std::string machine_type = machine_type_from_m115(m115_lines);
            if (machine_type.empty()) {
                result.error = "The printer did not report a model in M115. "
                               "Cannot confirm it is an INDX, so nothing was sent.";
                return result;
            }
            if (!machine_type_is_indx(machine_type)) {
                result.error = "Connected printer reports \"" + machine_type
                             + "\", which is not an INDX. This procedure sends "
                               "INDX-specific tool-change, temperature and motor-current "
                               "commands and must not run on another model. Nothing was sent.";
                return result;
            }
            BOOST_LOG_TRIVIAL(info) << "[Maintenance] confirmed INDX: " << machine_type;
        }

        // --- AUTO RETRACT. Firmware 6.9.0 removed the switch on INDX
        // (0dfee5ef1, BFW-8589): the toggle moved behind
        // HAS_SWITCHABLE_AUTO_RETRACT, which lists COREONE COREONEL MK4 iX XL
        // and neither INDX variant, so the menu item, the global-disable check
        // in maybe_retract_from_nozzle() and the auto_retract_enabled
        // config-store item are all compiled out. The only remaining lever is
        // the filament type: auto_retract.cpp returns early, ahead of any
        // retraction, for filaments marked is_flexible (BFW-6953), and FLEX is
        // the only preset with the flag.
        //
        // Marking the tool FLEX is a PERSISTENT write, so read the tool's real
        // filament type back first and put exactly that value back afterwards,
        // rather than assuming PLA and leaving the printer describing a spool
        // it does not have. M865 I<n> echoes "name:<TYPE>"; a tool with no
        // filament type set echoes nothing at all, which is reported rather
        // than silently defaulted.
        std::string orig_filament = "PLA";
        bool flex_applied = false;
        // Cleared when the tool reports a name this host cannot quote back into
        // M865; nothing is then written back. See the read-back below.
        bool restore_filament_type = true;
        {
            emit("Connecting", "Reading the tool's filament type");
            const std::string query = "M865 I" + std::to_string(options.tool);
            error_code ec;
            asio::write(serial, asio::buffer(query + "\n"), ec);
            if (ec) {
                result.error = "Could not read the tool's filament type: " + ec.message();
                return result;
            }
            std::string name;
            std::string query_err;
            if (!stream_until_ok(serial, io, 10'000, 10'000, nullptr,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[Maintenance] M865 << " << line;
                        constexpr std::string_view kKey = "name:";
                        if (line.compare(0, kKey.size(), kKey) != 0)
                            return;
                        // Capture VERBATIM. Filtering by character class here
                        // would make an odd name indistinguishable from no name
                        // at all, and the two want opposite handling.
                        std::string tail = line.substr(kKey.size());
                        while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.back())))
                            tail.pop_back();
                        while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.front())))
                            tail.erase(tail.begin());
                        name = tail;
                    }, query_err)) {
                result.error = "The printer did not answer " + query + " (" + query_err
                             + "). Cannot tell what filament type to restore, so nothing was sent.";
                return result;
            }

            // Three outcomes, and only the first may end in a value we chose.
            const bool sendable = !name.empty()
                && std::all_of(name.begin(), name.end(), [](unsigned char c) {
                       return std::isalnum(c) || c == '_' || c == '-';
                   });
            if (name.empty()) {
                // Not fatal, but the user has to know: there is no G-code that
                // sets a tool back to "no filament", so the tool ends up marked
                // PLA -- the filament this procedure has them insert.
                result.filament_type_was_unset = true;
                BOOST_LOG_TRIVIAL(warning)
                    << "[Maintenance] tool " << options.tool
                    << " has no filament type set; it will be left as PLA";
            } else if (sendable) {
                orig_filament = name;
                BOOST_LOG_TRIVIAL(info) << "[Maintenance] tool " << options.tool
                                        << " filament type: " << orig_filament;
            } else {
                // Cannot be quoted back into M865 (spaces, punctuation, a quote
                // character). Write nothing rather than overwrite it: the tool
                // stays FLEX and the UI hands the user the exact name back.
                restore_filament_type = false;
                result.unrestorable_filament_name = name;
                BOOST_LOG_TRIVIAL(warning)
                    << "[Maintenance] tool " << options.tool << " reports filament name \""
                    << name << "\", which cannot be sent back; leaving it marked FLEX";
            }
        }

        // Send one command and stream to "ok". `stage` drives the progress
        // dialog and `detail` describes the step in plain language. The caption
        // is emitted ONCE and then left alone: firmware chatter (temperature
        // reports and the like) goes to the debug log only, so the dialog shows
        // one stable line instead of a churning wall of text.
        auto run = [&](const char* stage, const std::string& cmd,
                       const char* detail,
                       unsigned overall_ms, unsigned idle_ms) -> bool {
            emit(stage, detail);
            error_code ec;
            asio::write(serial, asio::buffer(cmd + "\n"), ec);
            if (ec) {
                result.error = "Write failed (" + cmd + "): " + ec.message();
                return false;
            }
            BOOST_LOG_TRIVIAL(debug) << "[Maintenance] >> " << cmd;
            if (!stream_until_ok(serial, io, overall_ms, idle_ms, &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[Maintenance] << " << line;
                    }, err)) {
                if (is_force_stop()) {
                    send_emergency_stop();
                    result.error = "Force-stopped by user (printer requires reset).";
                    return false;
                }
                if (is_cancel()) {
                    result.cancelled = true;
                    result.error = "Cancelled";
                    return false;
                }
                result.error = std::string(stage) + " failed (" + cmd + "): " + err;
                return false;
            }
            return true;
        };

        // Best-effort state restore for the cooperative-cancel path. Short
        // timeouts, errors ignored: if an M0 dialog is still up on the printer
        // these queue behind it and execute once the user presses the knob.
        // Reached from both the cancel path and any command failure, so the
        // caption must not say "cancelling".
        //
        // Every command here is safety-critical, so success is TRACKED rather
        // than assumed: a dead port or a timeout mid-cleanup can leave the
        // nozzle hot with cold extrusion permitted and stall detection off.
        // Reporting "state restored" in that situation would be a lie the user
        // could get burned by, so restore_verified gates the UI message.
        auto restore_printer_state = [&]() {
            emit("Restoring", "Putting the printer back to a safe state");
            result.restore_attempted = true;
            std::vector<std::string> cleanup = {
                "M104 S0",   // heater off
                "M107",      // fan off
                "M302 S170", // restore cold-extrusion guard
                "M591 R",    // restore stuck-filament detection from EEPROM
                "M906 E550", // restore INDX default E current
            };
            // Conditional: M865 writes to the printer's persistent settings, so
            // sending it when the FLEX step was never reached would change a
            // tool this session never touched -- overwriting, say, a perfectly
            // good PETG setting with a value we invented.
            const std::string filament_restore_cmd_for_cleanup =
                "M865 S\"" + orig_filament + "\" L" + std::to_string(options.tool);
            if (flex_applied && restore_filament_type)
                cleanup.push_back(filament_restore_cmd_for_cleanup);
            cleanup.push_back("M84"); // release steppers
            bool all_confirmed = true;
            for (const std::string& cmd : cleanup) {
                // Escalation must work FROM here. Cleanup can block behind an
                // M0 prompt the user never answers, which is precisely when
                // they reach for Force Stop -- so poll the flag between
                // commands and hand it to the wait below. Leaving that wait
                // uninterruptible made the emergency stop useless exactly when
                // the printer was most likely to still be hot.
                if (is_force_stop()) {
                    send_emergency_stop();
                    return; // restore_verified stays false: cleanup is incomplete
                }
                error_code ec;
                asio::write(serial, asio::buffer(cmd + "\n"), ec);
                if (ec) {
                    BOOST_LOG_TRIVIAL(warning)
                        << "[Maintenance] cleanup write failed (" << cmd << "): " << ec.message();
                    all_confirmed = false;
                    break; // port gone; the rest cannot land either
                }
                std::string cleanup_err;
                // Pass the force-stop flag (NOT the combined cancel: we are
                // already cancelling, and cooperative cancel must not abort
                // the very cleanup it asked for).
                const bool acked = stream_until_ok(serial, io, 5'000, 5'000,
                                                   options.force_stop_requested,
                                                   [](const std::string&) {}, cleanup_err);
                if (!acked) {
                    if (is_force_stop()) {
                        send_emergency_stop();
                        return; // restore_verified stays false
                    }
                    BOOST_LOG_TRIVIAL(warning)
                        << "[Maintenance] cleanup unacknowledged (" << cmd << "): " << cleanup_err;
                    all_confirmed = false;
                    // Keep going: a later command may still land, and each one
                    // that does leaves the printer safer than stopping here.
                } else if (cmd == filament_restore_cmd_for_cleanup) {
                    result.filament_type_restored = true;
                }
            }
            result.restore_verified = all_confirmed;
        };

        // Timeout tiers. M0 prompt waits are user-paced: Buddy emits
        // "echo:busy:" keepalives while the dialog is up, so the idle timer
        // keeps refreshing; the overall cap just bounds a walked-away session
        // (the printer's own 30-min safety timer will have killed the heaters
        // long before this expires).
        constexpr unsigned kQuick   =    10'000; // parameter/setup commands
        constexpr unsigned kMove    =    60'000; // short E moves, dwells
        constexpr unsigned kPick    =   240'000; // T<n>: may home + travel
        constexpr unsigned kHeat    =   900'000; // M109 heat-up
        constexpr unsigned kCool    = 2'400'000; // M109 R deep-cool (fan-assisted)
        constexpr unsigned kPrompt  = 7'200'000; // M0 user prompts (2 h)
        constexpr unsigned kIdle    =    30'000; // per-byte idle cap everywhere

        const std::string tool  = std::to_string(options.tool);
        const std::string flush = std::to_string(options.flush_temp_c);
        const std::string pull  = std::to_string(options.pull_temp_c);
        // Two-stage warm-up stops short of the target so the PID does not
        // overshoot; see the Reheating steps below.
        const std::string pull_minus_3 = std::to_string(options.pull_temp_c - 3);
        const std::string pull_minus_4 = std::to_string(options.pull_temp_c - 4);

        // Built once so the step table and the loop that watches it agree on the
        // exact strings. If the tool genuinely held FLEX to begin with the two
        // are identical, which is harmless: the tool ends up FLEX either way,
        // and that IS its original state.
        const std::string flex_cmd            = "M865 S\"FLEX\" L" + tool;
        const std::string filament_restore_cmd = "M865 S\"" + orig_filament + "\" L" + tool;

        // --- SERIAL-PRINT TIMEOUT DISARM. Read this before touching the
        // command list below; it caused a BSOD ("E move without tool") on real
        // hardware.
        //
        // Buddy treats any G-command (and M73/M74/M109/M190) arriving over
        // serial as the start of an implicit "serial print"
        // (serial_printing.cpp:56-99). The inactivity timestamp is stamped when
        // that command is QUEUED, but the only thing that refreshes it,
        // SerialPrinting::print_loop(), runs solely from State::Printing --
        // which cannot be reached until the arming command finishes
        // (marlin_server.cpp:2207-2210).
        //
        // So arming with a LONG-BLOCKING command (M109 heat-up, or a G1 E move
        // that takes >5s) means the first timeout evaluation already sees the
        // whole blocking duration elapsed, instantly exceeding the 5s
        // serial_printing_screen_timeout (serial_printing.hpp:33). That runs
        // serial_print_finalize() -> pre_finalize_print(): auto-retract, nozzle
        // cleaner wipe, tool_change(NoTool) DOCK, disable_e_steppers. Every
        // later E move then hits bsod("E move without tool") at
        // planner.cpp:2177.
        //
        // Fix: arm with an INSTANT G-command first. G4 P1 (1 millisecond dwell)
        // is print-indicating via the 'G' branch, completes immediately, and so
        // lets the state reach Printing with a fresh timestamp. From then on the
        // timer is refreshed continuously, because GCodeQueue::advance()
        // decrements the queue length only AFTER process_next_command()
        // returns -- so has_commands_queued() stays true for the whole of a
        // blocking M109/M0/G4, and long user prompts are safe.
        //
        // Belt and braces: the pre-flight dialog also asks the user to turn off
        // Settings > Hardware > Experimental Settings > "Serial Printing
        // Screen", which stops serial_command_hook() from ever arming
        // (serial_printing.cpp:91-93); and re_pick below re-asserts the tool
        // before each extrusion block so a dock can never be followed by a
        // naked E move.
        const std::string arm_serial_print = "G4 P1";

        // Re-assert the tool immediately before any block of E moves.
        // tool_change repopulates tools.physical_tool, so even if something
        // docked the tool behind our back the next E move cannot hit the
        // assert. Idempotent when the tool is already picked.
        const std::string re_pick = "T" + tool + " M0";

        // --- The guided procedure. M0 messages: ≤95 chars, plain first word,
        // --- no semicolons (Buddy parser + MAX_CMD_SIZE rules).
        // `detail` is what the user reads in the progress dialog -- describe
        // the intent, never echo the gcode. The raw command still goes to the
        // debug log for diagnosis.
        struct Step {
            const char* stage;
            std::string cmd;
            const char* detail;
            unsigned    overall;
        };
        const Step steps[] = {
            { "Waiting at printer",
              "M0 Setup check: filament sensor OFF, PTFE tube removed. Press to continue",
              "Confirm the setup prompt on the printer screen", kPrompt },
            // Arm the serial-print state machine with an instant G-command so
            // it can never be armed later by a long-blocking one. See the
            // SERIAL-PRINT TIMEOUT DISARM note above.
            { "Preparing",    arm_serial_print, "Starting session",                        kQuick },
            { "Picking tool", "T" + tool + " M0",
              "Picking nozzle from its dock",                                              kPick  },
            // Suppress auto retract for the run. See the AUTO RETRACT note at
            // the model gate above for why this is the only lever left on 6.9.0.
            { "Preparing",    flex_cmd,
              "Marking the tool FLEX to suppress auto retract",                            kQuick },
            { "Preparing",    "M302 S0",  "Allowing cold extrusion",                       kQuick },
            { "Preparing",    "M83",      "Switching to relative extrusion",               kQuick },
            { "Preparing",    "M591 S0",  "Disabling stuck-filament detection",            kQuick },
            { "Waiting at printer",
              "M0 Insert light PLA into the top port until it stops at the drive gears. Do not force it",
              "Insert filament at the printer, then press the knob",                       kPrompt },
            { "Heating",      "M109 S" + flush,
              "Heating nozzle to flush temperature",                                       kHeat  },
            // Briefing before the purge: the screen and the nozzle cannot be
            // watched at once, so say what to look for, wait for the press,
            // then extrude. The prompt is unbounded, so re-assert after it.
            { "Waiting at printer",
              "M0 Purge next. Watch the nozzle tip for melted PLA flowing out. Press to start",
              "Read the purge prompt, then watch the nozzle tip",                          kPrompt },
            { "Purging",      "M109 S" + flush,
              "Confirming nozzle is still at temperature",                                 kHeat  },
            { "Purging",      re_pick,       "Confirming nozzle is picked",                kPick  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (1 of 3)",          kMove  },
            { "Purging",      "G4 S2",       "Letting pressure settle",                    kMove  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (2 of 3)",          kMove  },
            { "Purging",      "G4 S2",       "Letting pressure settle",                    kMove  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (3 of 3)",          kMove  },
            { "Waiting at printer",
              // The bail-out here is the slicer's Cancel button: over serial we
              // can restore printer state on cancel, unlike the print-job flow.
              "M0 Tip check. PLA out: press knob to continue. NOTHING out: press knob then cancel in slicer",
              "Check the nozzle tip, then answer the prompt on the printer",               kPrompt },
            // Prompts are user-paced and unbounded. Past the firmware's ~30 min
            // safety timer the heater target is cleared, so the temperature
            // before the prompt cannot be assumed to have survived it. Re-assert
            // and wait; a no-op when the nozzle is still at target.
            { "Packing",      "M109 S" + flush, "Confirming nozzle is still at temperature", kHeat },
            { "Packing",      "M104 S180",   "Cooling toward 180 C while packing",         kQuick },
            { "Packing",      re_pick,       "Confirming nozzle is picked",                kPick  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (1 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (2 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (3 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (4 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (5 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (6 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (7 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (8 of 8)",             kMove  },
            { "Deep cooling", "M104 S0",     "Turning the heater off",                     kQuick },
            { "Deep cooling", "M106 S255",   "Fan to full",                                kQuick },
            { "Deep cooling", "M109 R60",    "Cooling to 60 C",                            kCool  },
            { "Deep cooling", "M104 S0",     "Turning the heater off",                     kQuick },
            { "Deep cooling", "G4 S120",     "Holding 2 minutes to finish cooling",        180'000 },
            { "Deep cooling", "M107",        "Fan off",                                    kQuick },
            // Two-stage warm-up: aimed straight at the pull temperature, the
            // PID (not tuned for temperatures this low) overshoots and then
            // spends ~30s settling back. M109 C waits for a temperature without
            // changing the target (Buddy 6.6.2+; older firmware ignores it and
            // the next step waits as before).
            { "Reheating",    "M104 S" + pull_minus_3, "Warming toward pull temperature",  kQuick },
            { "Reheating",    "M109 C" + pull_minus_4, "Approaching from just below",      kHeat  },
            { "Reheating",    "M109 S" + pull,         "Settling at pull temperature",     kHeat  },
            { "Waiting at printer",
              "M0 Pull ready at " + pull + "C. Motor will yank the plug - keep hands clear. Press to pull",
              "Keep hands clear, then press the knob to pull",                             kPrompt },
            // Same unbounded-wait hazard, and worse here: yanking at high motor
            // current against a plug that has cooled to room temperature can
            // snap the filament or strain the drive. Re-establish the pull
            // temperature before committing to the pull.
            { "Pulling",      "M104 S" + pull_minus_3, "Restoring pull temperature",       kQuick },
            { "Pulling",      "M109 C" + pull_minus_4, "Approaching from just below",      kHeat  },
            { "Pulling",      "M109 S" + pull,         "Confirming pull temperature",      kHeat  },
            // Re-assert the tool after the long cool/reheat waits, which are
            // the likeliest window for an unnoticed dock.
            { "Pulling",      re_pick,          "Confirming nozzle is picked",             kPick  },
            { "Pulling",      "M906 E650",      "Raising extruder motor current",          kQuick },
            { "Pulling",      "G1 E-40 F3000",  "Pulling the plug out",                    kMove  },
            { "Pulling",      "G1 E-40 F1200",  "Feeding the strand up and out",           kMove  },
            { "Restoring",    "M906 E550",      "Restoring motor current",                 kQuick },
            { "Restoring",    "M591 R",         "Restoring stuck-filament detection",      kQuick },
            { "Restoring",    "M302 S170",      "Restoring cold-extrusion guard",          kQuick },
            { "Restoring",    "M104 S0",        "Turning the heater off",                  kQuick },
            { "Waiting at printer",
              "M0 Done. Remove the strand and inspect the tip. Press to warm the nozzle and dock the tool",
              "Remove the strand at the printer, then press the knob",                     kPrompt },
            // Warm dock, no manual parking. P0 S1 is the firmware's park-tool
            // G-code (what Prusa's own end G-code sends); it docks the picked
            // tool, stops its heater and is a no-op if nothing is picked.
            // Warming first matters: docked at ambient, solidified strings drag
            // through the dock instead of releasing. The wastebin brush wipe is
            // end-of-print machinery that never runs over serial, so the dock is
            // the only pass the nozzle gets here.
            { "Finishing",    re_pick,          "Confirming nozzle is picked",             kPick  },
            { "Finishing",    "M109 S170",
              "Warming the nozzle so residue releases in the dock",                        kHeat  },
            { "Finishing",    "M104 S0",        "Turning the heater off",                  kQuick },
            { "Finishing",    "P0 S1",          "Docking the tool",                        kPick  },
            { "Finishing",    "M84",            "Releasing the motors",                    kQuick },
            // Last, deliberately: FLEX has to stay in force through the warm-up
            // and the dock above, or the firmware would auto-retract the moment
            // the tool reads as PLA again. Safe to do at all here, unlike in the
            // G-code file, because a serial session has no end-of-print
            // sequence after it.
            { "Finishing",    filament_restore_cmd,
              "Restoring the tool's filament type",                                        kQuick },
        };

        // Map command groups onto the coarse 12-step progress scale so the
        // dialog advances meaningfully instead of once per G4.
        const char* last_stage = "";
        for (const Step& s : steps) {
            // Skip the restore outright when the reported name cannot be sent
            // back. Leaving the tool FLEX with a loud message in the result
            // beats writing a filament type nobody chose.
            if (!restore_filament_type && s.cmd == filament_restore_cmd)
                continue;
            if (std::strcmp(s.stage, last_stage) != 0) {
                last_stage = s.stage;
                if (step < kTotalSteps)
                    ++step;
            }
            if (is_cancel()) {
                if (is_force_stop()) {
                    send_emergency_stop();
                    result.error = "Force-stopped by user (printer requires reset).";
                    return result;
                }
                restore_printer_state();
                result.cancelled = true;
                result.error = "Cancelled";
                return result;
            }
            // Set BEFORE dispatch, not after the ok. If the write lands but
            // its ok is lost or arrives after the timeout, treating it as
            // un-applied would skip the restore and leave the tool marked FLEX
            // after a failed run. Over-approximating is safe: orig_filament was
            // read off this very tool before the write, so re-sending it is a
            // no-op if the write never landed.
            if (s.cmd == flex_cmd)
                flex_applied = result.filament_type_written = true;
            if (!run(s.stage, s.cmd, s.detail, s.overall, kIdle)) {
                // Any failure past this point can leave the printer hot with
                // its guards down -- a heating timeout is the obvious case:
                // nozzle still targeting the flush temperature, cold extrusion
                // permitted and stuck-filament detection off. Attempt cleanup
                // for every failure except a force stop, where M112 has already
                // halted the machine and further commands are pointless.
                //
                // Best-effort by design: if the port itself died the writes
                // simply fail and we fall through to reporting the original
                // error, which is the one worth showing the user.
                if (!is_force_stop())
                    restore_printer_state();
                return result;
            }
            if (s.cmd == filament_restore_cmd)
                result.filament_type_restored = true;
        }

        step = kTotalSteps;
        emit("Done", "Cold pull complete");
        return result;
    } catch (const std::exception& e) {
        result.error = explain_serial_error(port, e.what());
        return result;
    }
}

} // namespace Utils
} // namespace Slic3r
