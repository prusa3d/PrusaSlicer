///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Utils_PrinterTools_hpp_
#define slic3r_Utils_PrinterTools_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;

// The filament loaded in each tool, as a printer with Filament Slots reports it over PrusaLink
// (GET /api/v1/tools, CORE One INDX firmware). Free of wx, so the test binary can use it.

/// One tool (dock) of the printer. The firmware leaves out docks that aren't calibrated, so the
/// tools a printer reports can have gaps.
struct PrinterTool
{
    /// 0-based extruder index: JSON key "N" is extruder N-1. Never compacted over gaps.
    int         extruder_idx = -1;
    bool        loaded       = false;
    /// Filament type as the printer names it, e.g. "PETG", or a user type such as "PCTG".
    /// Empty when the tool isn't loaded.
    std::string material;
    /// Vendor and color names from the printer's filament library; empty when none was picked.
    std::string vendor;
    std::string color;
    /// "#RRGGBB" in uppercase, or empty when the printer reports no color.
    std::string color_hex;
    double      nozzle_diameter = 0.;
    bool        hardened        = false;
    bool        high_flow       = false;
};

struct PrinterTools
{
    /// In ascending extruder_idx.
    std::vector<PrinterTool> tools;
    /// Tool entries that were skipped or partly ignored, with the reason.
    std::vector<std::string> warnings;
    /// Extruder indices the printer listed but whose entries couldn't be read: their state is
    /// unknown, which isn't the same as a dock that isn't calibrated.
    std::vector<int>         unreadable;
};

enum class PrinterToolsStatus {
    Ok,
    /// The host type can't report tools, or the printer answered 404: its firmware has no
    /// Filament Slots (stock firmware, a printer other than the CORE One INDX).
    Unsupported,
    /// HTTP 401: wrong API key or password, or login disabled on the printer.
    AuthFailed,
    /// No HTTP answer: connection refused, timed out or dropped.
    Transport,
    /// Any other HTTP status from the printer.
    PrinterError,
    /// A 200 whose body isn't the expected JSON.
    Malformed,
};

struct PrinterToolsOptions
{
    long timeout_connect_s = 5;
    /// Total time for the request; the default of Http (none) would let a stalled printer
    /// block the caller forever.
    long timeout_max_s = 10;
};

struct PrinterToolsResult
{
    PrinterToolsStatus status      = PrinterToolsStatus::Transport;
    /// 0 when there was no HTTP answer.
    unsigned           http_status = 0;
    /// For anything but Ok: curl's error, a summary of the printer's status page, or why the
    /// body couldn't be read.
    std::string        message;
    PrinterTools       tools;
};

/// Asks the printer a physical printer's config points at (host_type, print_host, the API key or
/// the Digest user and password, with a "stored" password already resolved). Returns false when
/// its host type can't report tools, else true with the outcome in out.
bool fetch_printer_tools(const DynamicPrintConfig &physical_printer_config, PrinterToolsResult &out,
                         const PrinterToolsOptions &opts = {});

namespace printer_tools_detail {

/// Parses the body of GET /api/v1/tools, {"tools":{"1":{...},...}}. Returns false and sets
/// error when the body isn't JSON or has no "tools" object; tool entries that can't be read are
/// skipped with a warning in out.warnings. Never throws.
bool parse_tools_json(const std::string &body, PrinterTools &out, std::string &error);

/// One line for the user from a printer's status page: the JSON form's title and message, the
/// first non-empty line of the text form, or "HTTP <status>" when the body says nothing.
std::string status_page_summary(const std::string &body, unsigned status);

/// "#RRGGBB" in uppercase for a "#" followed by six hex digits of either case, else "".
std::string normalize_color_hex(const std::string &hex);

} // namespace printer_tools_detail

} // namespace Slic3r

#endif // slic3r_Utils_PrinterTools_hpp_
