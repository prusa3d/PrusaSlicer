///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPAPostProcessor.hpp"

#include "libslic3r/PrintConfig.hpp"   // GCodeFlavor

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r {

PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_notes)
{
    if (flavor == gcfKlipper)
        return PACalibrationCommand::Klipper;
    // Non-Marlin flavors that take a pressure-advance command (RepRapFirmware / Duet).
    if (flavor != gcfMarlinFirmware && flavor != gcfMarlinLegacy)
        return PACalibrationCommand::M572;
    // Marlin-flavored, which includes ALL Prusa printers. Prusa's Buddy input-shaper
    // firmware uses M572 (pressure advance); older firmware and generic Marlin use
    // M900 K (linear advance). Key off the SAME printer_notes markers Prusa's own
    // start_filament_gcode switches on -- the printer MODEL name is not reliable (e.g.
    // the MK3.9 carries PRINTER_MODEL_MK4IS in its notes, not "MK3.9"). Generic Marlin
    // printers carry none of these markers and correctly fall through to M900.
    static const std::regex m572_re(R"(MK4IS|XLIS|MK4S|MK3\.9S|MK3\.5|MINIIS|COREONE)",
                                    std::regex::icase);
    return std::regex_search(printer_notes, m572_re) ? PACalibrationCommand::M572
                                                     : PACalibrationCommand::M900;
}

const char *pa_command_prefix(PACalibrationCommand cmd)
{
    switch (cmd) {
    case PACalibrationCommand::Klipper: return "SET_PRESSURE_ADVANCE ADVANCE=";
    case PACalibrationCommand::M900:    return "M900 K";
    case PACalibrationCommand::M572:    break;
    }
    return "M572 S";
}

std::string format_pa_command(PACalibrationCommand cmd, double pa)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << pa_command_prefix(cmd) << std::fixed << std::setprecision(4) << pa;
    return s.str();
}

namespace {

// One PlaceholderParser tag "{...}" starting at s[pos] == '{'.
struct TemplateTag
{
    size_t end          = std::string::npos; // one past the closing '}'; npos if unterminated
    int    depth_change = 0;                 // +1 for {if ...}, -1 for {endif}
    bool   branch       = false;             // {elsif ...} or {else}
};

TemplateTag scan_template_tag(const std::string &s, size_t pos)
{
    TemplateTag tag;
    int         braces     = 0;
    bool        first_word = true;
    char        prev1 = 0, prev2 = 0; // the last two non-blank characters seen in the tag
    for (size_t i = pos; i < s.size();) {
        const char c = s[i];
        if (c == '"' || (c == '/' && prev1 == '~' && (prev2 == '=' || prev2 == '!'))) {
            // A string or a regex literal (after =~ / !~) is data, not template syntax: an
            // "if" or a brace inside e.g. {if printer_notes=~/endif/} must not count.
            for (++i; i < s.size() && s[i] != c; ++i)
                if (s[i] == '\\')
                    ++i;
            ++i; // past the closing quote / slash
            prev2      = prev1;
            prev1      = c;
            first_word = false;
        } else if (c == '{') {
            ++braces;
            ++i;
            prev2 = prev1;
            prev1 = c;
        } else if (c == '}') {
            ++i;
            if (--braces == 0) {
                tag.end = i;
                break;
            }
            prev2 = prev1;
            prev1 = c;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                ++j;
            const std::string word = s.substr(i, j - i);
            // Count every if/endif, so that a self-contained "{if a then b; endif}" nets to zero.
            if (word == "if")
                ++tag.depth_change;
            else if (word == "endif")
                --tag.depth_change;
            else if (first_word && (word == "elsif" || word == "else"))
                tag.branch = true;
            first_word = false;
            i          = j;
            prev2      = prev1;
            prev1      = s[j - 1];
        } else {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                prev2 = prev1;
                prev1 = c;
            }
            ++i;
        }
    }
    return tag;
}

// Whether the command at s[pos] is G-code that runs (in whichever template branch it sits):
// it starts a G-code word (at the line start, after whitespace or after a template tag), and no
// ';' comment precedes it on its line in a branch that is still open around it. A ';' inside an
// {if} block only comments out the rest of its own branch: in "{if MINI};{elsif ...}M900 K200"
// the M900 is live, in "{if MINI};M900 K200{endif}" it is not.
bool is_live_command(const std::string &s, size_t pos)
{
    if (pos > 0) {
        const char prev = s[pos - 1];
        if (prev != '\n' && prev != '\r' && prev != ' ' && prev != '\t' && prev != '}')
            return false;
    }
    const size_t nl         = s.rfind('\n', pos);
    const size_t line_start = nl == std::string::npos ? 0 : nl + 1;
    // commented[d]: a ';' was seen in the currently open branch at nesting depth d (0 = outside
    // any block opened on this line). A ';' comments out the rest of its own branch only.
    std::vector<bool> commented{ false };
    for (size_t i = line_start; i < pos;) {
        if (s[i] == '{') {
            const TemplateTag tag = scan_template_tag(s, i);
            // The command sits inside a template tag (e.g. a {if ...=~/M572 S/} condition):
            // that is template code, not G-code, and its "value" runs into the closing '}'.
            if (tag.end == std::string::npos || tag.end > pos)
                return false;
            const int depth = int(commented.size()) - 1;
            if (depth + tag.depth_change < 0 || (depth == 0 && tag.branch)) {
                // {elsif}/{else}/{endif} of a block opened on an earlier line: a ';' before
                // it on this line belongs to another branch.
                commented.assign(1, false);
            } else if (tag.depth_change > 0) {
                commented.insert(commented.end(), size_t(tag.depth_change), false); // {if}
            } else if (tag.depth_change < 0) {
                commented.resize(commented.size() - size_t(-tag.depth_change)); // {endif}
            } else if (tag.branch) {
                commented.back() = false; // {elsif} / {else}: a new branch starts
            }
            i = tag.end;
        } else {
            if (s[i] == ';')
                commented.back() = true;
            ++i;
        }
    }
    // Dead if a ';' earlier on the line comments out any of the branches still open around it.
    return std::find(commented.begin(), commented.end(), true) == commented.end();
}

// End of the command value starting at s[pos]: a number, a placeholder or a complete
// {if}...{endif} chain. The value ends at whitespace, a ';' comment or the end of the line, or
// before an {elsif}/{else}/{endif} of an enclosing block. Returns npos when the value is not a
// complete template (e.g. an {if} block spanning lines), which is not safe to replace.
size_t command_value_end(const std::string &s, size_t pos)
{
    int    depth = 0;
    size_t i     = pos;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '{') {
            const TemplateTag tag = scan_template_tag(s, i);
            if (tag.end == std::string::npos)
                return std::string::npos;
            if (depth + tag.depth_change < 0 || (depth == 0 && tag.branch))
                break;
            depth += tag.depth_change;
            i = tag.end;
        } else if (c == '\n' || c == '\r' ||
                   (depth == 0 && (c == ';' || c == ' ' || c == '\t'))) {
            break;
        } else
            ++i;
    }
    return depth == 0 ? i : std::string::npos;
}

// Whether the M900 K value s[begin, end) is in the legacy Linear Advance 1.0 scale. Prusa's
// MK3-era profiles pair an LA 1.5 line (K ~ 0.01-0.2) with an LA 1.0 fallback (K ~ 18-200):
// firmware 3.9+ picks its mode from the first non-zero K and then ignores K >= 10, while older
// firmware only understands the 1.0 scale. The value is LA 1.0 when every number it can output
// (its text outside template tags) is >= 10.
bool is_la10_value(const std::string &s, size_t begin, size_t end)
{
    bool any = false;
    for (size_t i = begin; i < end;) {
        if (s[i] == '{') {
            const TemplateTag tag = scan_template_tag(s, i);
            if (tag.end == std::string::npos || tag.end > end)
                return false;
            i = tag.end;
        } else if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            long long integer_part = 0;
            for (; i < end && std::isdigit(static_cast<unsigned char>(s[i])); ++i)
                integer_part = std::min<long long>(integer_part * 10 + (s[i] - '0'), 1000000);
            if (integer_part < 10)
                return false;
            any = true;
            while (i < end && (s[i] == '.' || std::isdigit(static_cast<unsigned char>(s[i]))))
                ++i;
        } else if (s[i] == '.') {
            return false; // ".05"
        } else
            ++i;
    }
    return any;
}

// Comment ending the line apply_pressure_advance_to_start_gcode() adds when the profile has no
// usable command of the printer's kind. It lets the next call find that line and drop it, so that
// e.g. switching to a printer with other firmware replaces the injected command instead of
// accumulating one line per firmware. Commands the profile itself contains are never marked.
constexpr const char *injected_pa_marker = " ; FilamentDB pressure advance";

bool is_injected_pa_line(std::string_view line)
{
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    const std::string_view marker = injected_pa_marker;
    if (line.size() < marker.size() || line.substr(line.size() - marker.size()) != marker)
        return false;
    for (const PACalibrationCommand cmd : { PACalibrationCommand::M572, PACalibrationCommand::M900,
                                            PACalibrationCommand::Klipper }) {
        const std::string_view prefix = pa_command_prefix(cmd);
        if (line.substr(0, prefix.size()) == prefix)
            return true;
    }
    return false;
}

// Remove the lines a previous call injected (see injected_pa_marker).
std::string remove_injected_pa_lines(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t start = 0;;) {
        const size_t nl   = s.find('\n', start);
        const bool   last = nl == std::string::npos;
        const std::string_view line(s.data() + start, (last ? s.size() : nl) - start);
        if (!is_injected_pa_line(line)) {
            out.append(line.data(), line.size());
            if (!last)
                out += '\n';
        }
        if (last)
            break;
        start = nl + 1;
    }
    return out;
}

} // namespace

std::string apply_pressure_advance_to_start_gcode(const std::string &gcode,
                                                  PACalibrationCommand cmd,
                                                  double pa)
{
    // Builds before this fix appended "\\n" + "M572 S" + std::to_string(pa): a literal
    // backslash-n, so that command never ran. Drop those leftovers from saved presets.
    static const std::regex legacy_append_re(R"(\\nM572 S\d+[.,]\d{6})");
    std::string out = remove_injected_pa_lines(std::regex_replace(gcode, legacy_append_re, ""));

    const std::string prefix  = pa_command_prefix(cmd);
    const std::string command = format_pa_command(cmd, pa);
    const std::string value   = command.substr(prefix.size());

    bool replaced = false;
    bool kept_la10 = false;
    for (size_t pos = out.find(prefix); pos != std::string::npos; pos = out.find(prefix, pos)) {
        const size_t value_start = pos + prefix.size();
        const size_t value_end   = is_live_command(out, pos) ? command_value_end(out, value_start)
                                                             : std::string::npos;
        if (value_end == std::string::npos) {
            pos = value_start;
            continue;
        }
        if (cmd == PACalibrationCommand::M900 && is_la10_value(out, value_start, value_end)) {
            // Keep the LA 1.0 fallback: a (LA 1.5 scale) calibration value would be wrong there.
            kept_la10 = true;
            pos       = value_end;
            continue;
        }
        out.replace(value_start, value_end - value_start, value);
        replaced = true;
        pos      = value_start + value.size();
    }

    if (!replaced) {
        const std::string injected = command + injected_pa_marker;
        if (kept_la10) {
            // Only LA 1.0 lines: put the LA 1.5 value first, so that LA 1.5 firmware picks its
            // mode from it and ignores the 1.0 values, while older firmware still ends on them.
            out.insert(0, injected + "\n");
        } else {
            if (!out.empty() && out.back() != '\n')
                out += '\n';
            out += injected;
        }
    }
    return out;
}

} // namespace Slic3r
