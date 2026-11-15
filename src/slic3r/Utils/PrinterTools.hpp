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
// (GET /api/v1/tools, CORE One INDX firmware), or as Prusa Connect has it for a printer (GET
// /slicer/v2/printers): what the printer reported, or the material assigned to the tool in
// Connect's Tools page. Free of wx, so the test binary can use it.

/// One tool (dock) of the printer. The firmware leaves out docks that aren't calibrated, so the
/// tools a printer reports can have gaps.
struct PrinterTool
{
    /// 0-based extruder index: PrusaLink's JSON key "N" is extruder N-1, Prusa Connect's is
    /// extruder N. Never compacted over gaps.
    int         extruder_idx = -1;
    bool        loaded       = false;
    /// Filament type as the printer names it, e.g. "PETG", or a user type such as "PCTG".
    /// Empty when the tool isn't loaded.
    std::string material;
    /// Vendor and color names from the printer's filament library; empty when none was picked.
    /// From Prusa Connect: the brand of the assigned material, and the printer's color name for it
    /// (printer_tools_detail::printer_colour()).
    std::string vendor;
    std::string color;
    /// "#RRGGBB" in uppercase, or empty when the printer reports no color.
    std::string color_hex;
    /// The product name of a material assigned in Prusa Connect, without the brand ("Pro PCTG,
    /// Translucent Cyan"); empty from PrusaLink and when Connect knows only the type.
    std::string product;
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
    /// Who reported the tools, as the preview's notes name it: empty for the printer itself,
    /// "Prusa Connect" for what Connect has for it.
    std::string              source;
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

/// A printer of the user's Prusa Connect account, from GET /slicer/v2/printers.
struct ConnectPrinter
{
    std::string              uuid;
    /// The name the user gave it in Connect; may be empty.
    std::string              name;
    /// connect_state: "IDLE", "PRINTING", "OFFLINE"...
    std::string              state;
    /// Connect's model ("COREONE_INDX"), and the printer_model of the PrusaSlicer profile it
    /// corresponds to ("COREONE_INDX8T").
    std::string              model;
    std::string              legacy_model;
    /// printer_model values of the PrusaSlicer profiles it can print, as Connect lists them.
    std::vector<std::string> supported_models;
    /// The tools are an MMU's slots ("0.0".."0.4") rather than extruders: not read.
    bool                     has_feeder_slots = false;
    PrinterTools             tools;
};

struct ConnectPrintersResult
{
    /// Unsupported: Connect answered 404, it has no such printer list. AuthFailed: 401, the
    /// Prusa Account access token is out of date. PrinterError: any other HTTP status from Connect,
    /// 403 (access refused) too, with Connect's message.
    PrinterToolsStatus          status      = PrinterToolsStatus::Transport;
    unsigned                    http_status = 0;
    std::string                 message;
    std::vector<ConnectPrinter> printers;
    /// Printer entries that were skipped, with the reason.
    std::vector<std::string>    warnings;
};

/// GET url (Prusa Connect's /slicer/v2/printers) with the Prusa Account access token.
void fetch_connect_printers(const std::string &url, const std::string &access_token, ConnectPrintersResult &out,
                            const PrinterToolsOptions &opts = {});

/// Whether a Connect printer is one a printer profile with this printer_model prints for: its
/// legacy_printer_model or one of its supported_printer_models, ignoring case. False for an empty
/// printer_model.
bool connect_printer_fits(const ConnectPrinter &printer, const std::string &printer_model);

namespace printer_tools_detail {

/// Parses the body of Prusa Connect's GET /slicer/v2/printers, {"printers":[...]} (or that object
/// alone in an array). Returns false and sets error when the body isn't JSON or has no "printers"
/// list; printers without a uuid are skipped with a warning in warnings, and tool entries that
/// can't be read are skipped with a warning in the printer's tools. Never throws.
bool parse_connect_printers_json(const std::string &body, std::vector<ConnectPrinter> &out,
                                 std::vector<std::string> &warnings, std::string &error);

/// "#RRGGBB" in uppercase for "#" followed by six or eight hex digits of either case (Connect's
/// "#rrggbbaa": the alpha is dropped), else "".
std::string normalize_color_rgba(const std::string &rgba);

/// The printer's name for a tool's color, from the 16 the printer has, capitalized as it spells
/// them: the one the product name names ("Cyan" in "Pro PCTG, Translucent Cyan", "Gray" for
/// "Grey"); of several ("PETG Black Silver Glitter"), the one closest to color_hex; with none, or
/// no product (a color the printer reported), the one whose color is exactly color_hex, as the
/// printer names its own colors. "" when none fits.
std::string printer_colour(const std::string &product, const std::string &color_hex);

/// One line for the user from Prusa Connect's error reply: its "message", or "HTTP <status>".
std::string connect_error_summary(const std::string &body, unsigned status);

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
