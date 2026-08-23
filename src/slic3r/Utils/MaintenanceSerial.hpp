///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MaintenanceSerial_hpp_
#define slic3r_MaintenanceSerial_hpp_

#include <atomic>
#include <functional>
#include <string>

namespace Slic3r {
namespace Utils {

// Progress update during a maintenance procedure. Called from the worker
// thread, so the UI caller is responsible for marshalling to the main thread.
// Mirrors BedMeshProbeProgress; kept separate so the two features stay
// decoupled.
struct MaintenanceProgress
{
    std::string stage;   // "Connecting", "Heating", "Purging", "Waiting at printer", ...
    std::string detail;  // free text: "T:150/290", firmware line, prompt hint, etc.
    int         step = 0;        // completed steps of the whole procedure
    int         total_steps = 0; // total steps (fixed per procedure), 0 → pulse
};

using MaintenanceProgressCallback = std::function<void(const MaintenanceProgress&)>;

struct MaintenanceResult
{
    std::string port_used; // the port we connected to, for logging
    std::string error;     // empty on success, human-readable on failure
    bool        cancelled = false; // true when the user cancelled cooperatively

    // True only when M112 was actually written to the port. Requesting a force
    // stop is not the same as delivering one -- the worker can fail and exit
    // while the user is still choosing -- and the UI must not claim an
    // emergency stop that never reached the printer.
    bool        force_stopped = false;

    // Set when a cleanup pass ran. restore_verified is true only if EVERY
    // safety-critical cleanup command was acknowledged by the printer. If the
    // port died or a command timed out mid-cleanup the printer may still be
    // hot with cold extrusion permitted, so the UI must say "check the
    // printer" rather than "state restored".
    bool        restore_attempted = false;
    bool        restore_verified  = false;

    // The tool had no filament type set when the procedure started. Auto retract
    // is suppressed by marking the tool FLEX (see the AUTO RETRACT note in the
    // .cpp), and there is no G-code that sets a tool back to "no filament", so
    // the tool ends up marked PLA -- the filament this procedure has the user
    // insert. Accurate, but not what was there before, so the UI says so.
    bool        filament_type_was_unset = false;

    // The M865 filament-type write was dispatched, so this session may have
    // changed a persistent printer setting. Pairs with the two flags above: it
    // is what makes the "now marked PLA" notice worth showing, and what makes an
    // unverified restore worth warning about (the tool may still read FLEX).
    // Every terminal path has to disclose this, not just the successful one --
    // a cancelled run changes the setting just as permanently.
    bool        filament_type_written = false;

    // The tool's original filament type was written back AND acknowledged,
    // either by the run's own last step or by cleanup. Paired with
    // filament_type_written this is what tells the UI whether the tool is back
    // where it started or may still read FLEX. It has to be tracked separately
    // from restore_verified, because a force stop returns without attempting
    // cleanup at all -- so "cleanup was not attempted" must not be mistaken for
    // "nothing was left changed".
    bool        filament_type_restored = false;

    // The tool reported a filament name this host cannot quote back into M865 --
    // the firmware will store an NFC abbreviation that fails its own name
    // validation (openprinttag/data_utils.cpp keeps the name and marks the data
    // unsafe), so this is reachable in practice. Nothing is written back in that
    // case: the tool is left marked FLEX and the UI hands the user the exact
    // name to restore by hand, because overwriting it with a value we chose
    // would destroy the very thing the read-back exists to preserve.
    std::string unrestorable_filament_name;
};

// Options for the INDX cold-pull procedure. Defaults match the recipe verified
// against Buddy v6.6.3 and Prusa support guidance (290 flush / 80 pull).
struct ColdPullOptions
{
    // Physical dock index of the tool (nozzle) to clean, 0-based. Sent as
    // "T<n> M0" (M0 parameter = bypass tool mapping).
    int tool = 0;

    // Flush temperature (°C, INDX displayed frame). Buddy clamps settable
    // targets to 290 on Core One (HEATER_0_MAXTEMP 305 − 15 margin); values
    // above that would make M109 wait forever, so the dialog caps at 290.
    int flush_temp_c = 290;

    // Pull temperature (°C). Working band 80–120; 80 is hardware-verified.
    int pull_temp_c = 80;

    // Explicit USB serial device path. Empty → auto-detect (Prusa VID).
    std::string explicit_port;

    // Cooperative cancel: polled between phases and honored inside long
    // streams. On cancel the worker restores printer state (heaters off,
    // cold-extrusion guard + stuck-filament detection + E current restored)
    // before disconnecting — this is the supported mid-procedure bail-out,
    // e.g. when the purge shows the blockage is above the melt zone.
    std::atomic<bool>* cancel_requested = nullptr;

    // Force stop: sends M112 (emergency stop) and disconnects. The printer
    // needs a reset afterwards. Use only when cooperative cancel is stuck.
    std::atomic<bool>* force_stop_requested = nullptr;
};

// Run the guided INDX cold-pull procedure over USB serial.
//
// The interaction prompts (insert filament, tip check, pull confirmation,
// strand removal) are shown ON THE PRINTER's screen via M0 QuickPause
// dialogs — the user must be at the machine and confirm each with a knob
// press; the slicer-side progress dialog mirrors the current stage. Buddy
// emits "echo:busy:" keepalives while an M0 dialog is up, so the serial
// stream stays alive for however long the user takes (bounded by generous
// overall timeouts below).
//
// Serial-session differences vs. running the same recipe as a USB print job
// (both verified against Buddy v6.6.3):
//  - No end-of-print machinery runs, so there is no wastebin brush wipe. The
//    procedure warms the nozzle and docks the tool itself with P0 S1, ending
//    with heaters off, steppers disabled and the tool parked. It is also why
//    the serial path CAN restore the filament type it changed (see below):
//    nothing runs after the last command that could auto-retract.
//  - There is no tool-mapping layer; "T<n> M0" is belt-and-braces.
//  - Print-time systems (runout-triggered pause, in-print nozzle checks) are
//    inactive; the printer-side Settings prerequisites still apply and are
//    re-checked by the first on-printer prompt: Filament Sensor OFF (blocks
//    autoload while hand-inserting filament). Auto Retract has no switch on
//    INDX from firmware 6.9.0 (0dfee5ef1); the procedure suppresses it by
//    marking the tool FLEX, reads the tool's real filament type first and
//    restores it as the very last command of the run.
//
// This is synchronous and typically takes 10–20 minutes end to end (dominated
// by the deep-cool phase and user response time). Call from a worker thread.
MaintenanceResult run_cold_pull(const MaintenanceProgressCallback& progress,
                                const ColdPullOptions& options = {});

// Return the USB serial port of a connected Prusa printer, or an empty string
// if none is found. Detection only enumerates ports (it does not open one), so
// it is cheap enough to call while building a dialog.
//
// Note this reports that a printer is *plugged in*, not that it is idle or
// willing to take commands -- a port held by another application still scans
// as present and only fails when actually opened.
std::string detect_printer_port();

// Render the same cold-pull procedure as a standalone G-code file, to be run as
// an ordinary print job from a USB drive or uploaded to the printer.
//
// This is the SAFER of the two delivery paths and the one to prefer. A print job
// is driven by the media queue and managed by the normal print state machine, so
// it is immune to the serial-print inactivity timeout that makes the streamed
// variant fragile (see the SERIAL-PRINT TIMEOUT DISARM note in the .cpp).
//
// Differences from the streamed variant, all deliberate:
//  - The bail-out at the tip check is the printer's own Stop, not a host-side
//    Cancel, so the prompt says so. Stopping there skips the restore block --
//    the header documents the two commands to run afterwards.
//  - The end-of-print sequence DOES run when the job finishes (nozzle wipe,
//    tool dock, steppers off), which is exactly what is wanted at the end, so
//    the final prompt asks the user to clear the strand before it starts. The
//    file rewarms the nozzle to 170 first so the wipe lifts softened residue
//    instead of dragging cold strings.
//  - It CANNOT restore the filament type it changes to suppress auto retract:
//    the end-of-print sequence runs after the last line, and a tool reading as
//    PLA there would auto-retract, reheating over the warm-wipe temperature
//    and ramming the melt zone just cleared. The header of the generated file
//    and the final on-screen text both tell the user to put it back.
//  - Every M0 message is kept within the 95-character command limit
//    (MAX_CMD_SIZE 96) and starts with a plain word containing no semicolons,
//    or the firmware crops it mid-sentence and raises a warning.
std::string generate_cold_pull_gcode(const ColdPullOptions& options);

} // namespace Utils
} // namespace Slic3r

#endif // slic3r_MaintenanceSerial_hpp_
