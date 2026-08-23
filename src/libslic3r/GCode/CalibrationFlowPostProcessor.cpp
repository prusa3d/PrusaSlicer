///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationFlowPostProcessor.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/format.hpp"

#include <LocalesUtils.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r {

static constexpr const char *BUILTIN_PREFIX = "::builtin::flow_calibration";

bool is_calibration_flow_url(const std::string &script)
{
    return boost::starts_with(script, BUILTIN_PREFIX);
}

std::string make_calibration_flow_url(const std::vector<std::pair<std::string, double>> &objects)
{
    // Force the C numeric locale so the URL always uses '.' decimals.
    // PrusaSlicer does not pin LC_NUMERIC globally, and the URL is
    // comma-delimited — a comma decimal separator would make it ambiguous.
    CNumericLocalesSetter locales_setter;

    std::ostringstream out;
    out << BUILTIN_PREFIX << "?objects=";
    char buf[32];
    for (size_t i = 0; i < objects.size(); ++i) {
        if (i != 0) out << ',';
        out << objects[i].first << ':';
        std::snprintf(buf, sizeof(buf), "%.5f", objects[i].second);
        out << buf;
    }
    return out.str();
}

namespace {

// name -> E scale factor.
using FactorMap = std::map<std::string, double>;

// For Klipper flavor, LabelObjects::init rewrites object-label names before
// emitting EXCLUDE_OBJECT markers (it replaces a set of special characters
// with '_'), so a pad named "-.05" reaches the G-code as "__05". Mirror that
// substitution here so the factor table can be matched against the rewritten
// names too. Keep this banned set in sync with LabelObjects.cpp.
std::string klipper_sanitize(const std::string &name)
{
    static const std::string banned = "\b\t\n\v\f\r \"#%&\'*-./:;<>\\";
    std::string out = name;
    std::replace_if(out.begin(), out.end(),
                    [](char c) { return banned.find(c) != std::string::npos; }, '_');
    return out;
}

// Throws on malformed URL.
FactorMap parse_url(const std::string &url)
{
    auto qpos = url.find('?');
    if (qpos == std::string::npos)
        throw Slic3r::RuntimeError(format("flow_calibration URL has no query string: %1%", url));

    FactorMap factors;
    bool      have_objects = false;

    std::string query = url.substr(qpos + 1);
    size_t      pos   = 0;
    while (pos < query.size()) {
        size_t      amp = query.find('&', pos);
        std::string kv  = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        pos             = amp == std::string::npos ? query.size() : amp + 1;

        size_t eq = kv.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);

        if (key == "objects") {
            size_t lp = 0;
            while (lp < val.size()) {
                size_t      comma = val.find(',', lp);
                std::string pair  = val.substr(lp, comma == std::string::npos ? std::string::npos : comma - lp);
                lp                = comma == std::string::npos ? val.size() : comma + 1;
                // Split at the LAST ':' so an object name may itself contain ':'.
                size_t colon = pair.rfind(':');
                if (colon == std::string::npos)
                    throw Slic3r::RuntimeError(format("flow_calibration object missing ':': %1%", pair));
                std::string name   = pair.substr(0, colon);
                double      factor = std::stod(pair.substr(colon + 1));
                factors[name]      = factor;
                // Also key on the Klipper-rewritten form (e.g. "-.05" -> "__05")
                // so EXCLUDE_OBJECT names match. Never overwrite a raw name.
                factors.emplace(klipper_sanitize(name), factor);
            }
            have_objects = true;
        }
    }

    if (!have_objects || factors.empty())
        throw Slic3r::RuntimeError(format("flow_calibration URL missing objects: %1%", url));
    return factors;
}

// Locate an axis term in a G-code line: the axis letter must start the token
// (preceded by whitespace or be at the start) and be followed by a number.
// Searches the part before any ';' comment. On a hit, returns true and writes
// the [begin, end) span of the numeric substring (excluding the axis letter)
// plus its parsed value.
bool find_axis_span(const std::string &line, char axis, size_t &num_begin, size_t &num_end, double &value)
{
    size_t       comment = line.find(';');
    const size_t bound   = comment == std::string::npos ? line.size() : comment;
    size_t       i       = 0;
    while (i < bound) {
        if ((line[i] == axis || line[i] == char(std::tolower(axis))) &&
            (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
            size_t j         = i + 1;
            size_t start_num = j;
            if (j < bound && (line[j] == '+' || line[j] == '-'))
                ++j;
            bool any = false;
            while (j < bound && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) {
                any = true;
                ++j;
            }
            if (any) {
                num_begin = start_num;
                num_end   = j;
                value     = std::stod(line.substr(start_num, j - start_num));
                return true;
            }
        }
        ++i;
    }
    return false;
}

bool has_axis(const std::string &line, char axis)
{
    size_t b, e;
    double v;
    return find_axis_span(line, axis, b, e, v);
}

// A move that deposits material: G0/G1/G2/G3 with positive E and some XY
// motion. Pure-E moves (retract/deretract/prime) have no X/Y and are skipped,
// so retraction lengths are never touched. Travels have no E. Arc moves
// (G2/G3) always carry X/Y in PrusaSlicer output, so the XY test covers them.
bool is_deposition_move(const std::string &line, double &e_begin_out, double &e_end_out, double &e_value_out)
{
    if (line.size() < 3 || line[0] != 'G')
        return false;
    char c = line[1];
    if (c != '0' && c != '1' && c != '2' && c != '3')
        return false;
    // Reject G10/G11/G17/... where a digit follows (e.g. "G17").
    if (std::isdigit(static_cast<unsigned char>(line[2])))
        return false;

    size_t b, e;
    double v;
    if (!find_axis_span(line, 'E', b, e, v) || v <= 0.0)
        return false;
    if (!has_axis(line, 'X') && !has_axis(line, 'Y'))
        return false;
    e_begin_out = double(b);
    e_end_out   = double(e);
    e_value_out = v;
    return true;
}

// Returns 83 for an M83 line (relative E), 82 for M82 (absolute E), 0 otherwise.
int extrusion_mode_command(const std::string &line)
{
    if (line.size() < 3 || line[0] != 'M' || line[1] != '8')
        return 0;
    if (line[2] != '2' && line[2] != '3')
        return 0;
    if (line.size() > 3) {
        char c = line[3];
        if (!std::isspace(static_cast<unsigned char>(c)) && c != ';')
            return 0;
    }
    return line[2] == '2' ? 82 : 83;
}

// Parse "M486 S<id>" -> id (>=0, or -1 for stop). Returns false if not such a
// line. RepRapFirmware emits the header definition on a single line as
// `M486 S<id> A"<name>"`; if such an inline name is present it is returned via
// inline_name_out / has_inline_name_out (quotes stripped). Marlin uses two
// lines (`M486 S<id>` then `M486 A<name>`), in which case no inline name.
bool parse_m486_s(const std::string &line, int &id_out,
                  std::string &inline_name_out, bool &has_inline_name_out)
{
    has_inline_name_out = false;
    if (!boost::starts_with(line, "M486"))
        return false;
    size_t i = 4;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size() || (line[i] != 'S' && line[i] != 's'))
        return false;
    ++i;
    size_t start = i;
    if (i < line.size() && line[i] == '-') ++i;
    bool any = false;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) { any = true; ++i; }
    if (!any)
        return false;
    id_out = std::stoi(line.substr(start, i - start));

    // Look for an inline `A<name>` token after the S<id> (RepRapFirmware form).
    while (i < line.size()) {
        if ((line[i] == 'A' || line[i] == 'a') && std::isspace(static_cast<unsigned char>(line[i - 1]))) {
            std::string name = line.substr(i + 1);
            boost::trim(name);
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                name = name.substr(1, name.size() - 2);
            inline_name_out     = std::move(name);
            has_inline_name_out = true;
            break;
        }
        ++i;
    }
    return true;
}

// Parse "M486 A<name>" -> name (the rest of the line, trimmed; surrounding
// quotes stripped for the RepRapFirmware `A"name"` form). Returns false if not
// such a line.
bool parse_m486_a(const std::string &line, std::string &name_out)
{
    if (!boost::starts_with(line, "M486"))
        return false;
    size_t i = 4;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size() || (line[i] != 'A' && line[i] != 'a'))
        return false;
    ++i;
    std::string name = line.substr(i);
    boost::trim(name);
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    name_out = std::move(name);
    return true;
}

// OctoPrint object-label comments carry the name plus an " id:<n> copy <m>"
// suffix (LabelObjects::init appends it). Recover the bare object name by
// stripping that trailing suffix.
std::string octoprint_object_name(const std::string &after_prefix)
{
    std::string name = after_prefix;
    boost::trim(name);
    size_t id = name.rfind(" id:");
    if (id != std::string::npos && name.find(" copy ", id) != std::string::npos)
        name = name.substr(0, id);
    return name;
}

// Parse a Klipper EXCLUDE_OBJECT_START NAME='...' / NAME=... line.
bool parse_exclude_object_start(const std::string &line, std::string &name_out)
{
    if (!boost::starts_with(line, "EXCLUDE_OBJECT_START"))
        return false;
    size_t n = line.find("NAME=");
    if (n == std::string::npos)
        return false;
    std::string name = line.substr(n + 5);
    boost::trim(name);
    if (!name.empty() && (name.front() == '\'' || name.front() == '"')) {
        char q   = name.front();
        size_t e = name.find(q, 1);
        name     = name.substr(1, e == std::string::npos ? std::string::npos : e - 1);
    }
    name_out = std::move(name);
    return true;
}

} // namespace

bool run_calibration_flow_post_processor(const std::string &url, const std::string &gcode_path)
{
    BOOST_LOG_TRIVIAL(info) << "flow_calibration: starting in-process post-process on " << gcode_path;

    // Force the C numeric locale for the whole pass: parse_url() uses stod(),
    // axis parsing uses stod(), and the rewritten E values are formatted with
    // snprintf() — all of which must use '.' decimals regardless of the user's
    // system locale (PrusaSlicer does not pin LC_NUMERIC globally).
    CNumericLocalesSetter locales_setter;

    FactorMap factors = parse_url(url);

    boost::filesystem::path src(gcode_path);

    // Pass 1 — confirm this G-code is actually a flow calibration print. The
    // `post_process` hook persists in the print preset, so this runs for EVERY
    // slice made with that preset. Only a G-code carrying the job-scoped
    // calibration marker should be rewritten; anything else is passed through
    // untouched.
    {
        boost::nowide::ifstream scan(gcode_path);
        if (!scan.is_open())
            throw Slic3r::RuntimeError(format("flow_calibration: cannot open %1%", gcode_path));
        const std::string marker = calibration_flow_marker();
        bool              found  = false;
        std::string       l;
        while (std::getline(scan, l)) {
            if (l.find(marker) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            BOOST_LOG_TRIVIAL(info) << "flow_calibration: marker absent, leaving "
                                    << gcode_path << " unchanged";
            return false;
        }
    }

    // Defensive: a marker-carrying file should always be ASCII (the dialog
    // forces binary_gcode=false). Detect binary G-code anyway and refuse
    // loudly rather than scribbling over a binarized payload.
    {
        boost::nowide::ifstream sniff(gcode_path, std::ios::binary);
        char magic[4] = {};
        sniff.read(magic, 4);
        if (sniff.gcount() == 4 && std::string(magic, 4) == "GCDE")
            throw Slic3r::RuntimeError(format(
                "flow_calibration: input %1% is binary G-code (.bgcode); "
                "the in-process rewriter only operates on ASCII. Set binary_gcode=false "
                "on the printer config for calibration prints.",
                gcode_path));
    }

    boost::filesystem::path tmp = src;
    tmp += ".flowcal.tmp";

    boost::nowide::ifstream in(gcode_path);
    if (!in.is_open())
        throw Slic3r::RuntimeError(format("flow_calibration: cannot open %1%", gcode_path));
    boost::nowide::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw Slic3r::RuntimeError(format("flow_calibration: cannot write %1%", tmp.string()));

    // M486 emits its object table up front: "M486 S<id>\nM486 A<name>\nM486 S-1"
    // per object (see LabelObjects::all_objects_header). We learn id->factor
    // from that header, then apply by id in the body. EXCLUDE_OBJECT_START /
    // "; printing object" carry the name directly. Object ids are assigned in
    // pointer order, so we never key off the numeric id alone — always name.
    std::map<int, double> id_to_factor;
    double                current_factor   = 1.0;  // 1.0 == outside any pad, or unknown pad
    bool                  relative_e       = true; // dialog forces relative E; track M82/M83 anyway
    int                   def_id_pending   = -2;   // M486 S<id> awaiting its M486 A<name> on the next line
    size_t                rewrites         = 0;
    std::string           line;
    line.reserve(256);

    auto factor_for_name = [&](const std::string &name) -> double {
        auto it = factors.find(name);
        return it == factors.end() ? 1.0 : it->second;
    };

    while (std::getline(in, line)) {
        // Strip trailing CR if present (Windows line endings).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // An "M486 A<name>" immediately following an "M486 S<id>" defines that
        // object's name (header block). Consume the pending id for this line only.
        const int await_def = def_id_pending;
        def_id_pending       = -2;

        int         m486_id;
        std::string obj_name;
        std::string inline_name;
        bool        has_inline_name = false;
        if (parse_m486_s(line, m486_id, inline_name, has_inline_name)) {
            if (m486_id < 0) {
                current_factor = 1.0;            // M486 S-1: stop object
            } else {
                if (has_inline_name)             // RRF single-line definition: M486 S<id> A"<name>"
                    id_to_factor[m486_id] = factor_for_name(inline_name);
                else
                    def_id_pending = m486_id;    // Marlin: M486 A<name> follows on the next line
                auto it        = id_to_factor.find(m486_id);
                current_factor = it == id_to_factor.end() ? 1.0 : it->second;
            }
            out << line << '\n';
            continue;
        }
        if (parse_m486_a(line, obj_name)) {
            if (await_def >= 0)
                id_to_factor[await_def] = factor_for_name(obj_name);
            out << line << '\n';
            continue;
        }
        if (parse_exclude_object_start(line, obj_name)) {
            current_factor = factor_for_name(obj_name);
            out << line << '\n';
            continue;
        }
        if (boost::starts_with(line, "EXCLUDE_OBJECT_END")) {
            current_factor = 1.0;
            out << line << '\n';
            continue;
        }
        if (boost::starts_with(line, "; printing object ")) {
            current_factor = factor_for_name(
                octoprint_object_name(line.substr(std::string("; printing object ").size())));
            out << line << '\n';
            continue;
        }
        if (boost::starts_with(line, "; stop printing object")) {
            current_factor = 1.0;
            out << line << '\n';
            continue;
        }

        // Extrusion-distance mode tracking.
        if (int mode = extrusion_mode_command(line); mode != 0)
            relative_e = (mode == 83);

        // Scale the deposited E inside a pad with a non-unity factor. The
        // center pad (factor 1.0) and all non-pad moves stay byte-identical.
        if (std::abs(current_factor - 1.0) > 1e-9) {
            double e_begin, e_end, e_val;
            if (is_deposition_move(line, e_begin, e_end, e_val)) {
                // Scaling a single relative-E delta scales that move's
                // deposition exactly. In absolute-E mode E is cumulative, so a
                // per-move scale would corrupt the running position — refuse.
                if (!relative_e)
                    throw Slic3r::RuntimeError(format(
                        "flow_calibration: %1% uses absolute E distances (M82); "
                        "the rewriter requires relative E (M83). Set "
                        "use_relative_e_distances=true on the printer config.",
                        gcode_path));
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.5f", e_val * current_factor);
                line = line.substr(0, size_t(e_begin)) + buf + line.substr(size_t(e_end));
                ++rewrites;
            }
        }

        out << line << '\n';
    }

    in.close();
    out.close();

    boost::system::error_code ec;
    boost::filesystem::rename(tmp, src, ec);
    if (ec) {
        // Fallback: copy + remove. rename can fail across filesystems.
        boost::filesystem::copy_file(tmp, src, boost::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            throw Slic3r::RuntimeError(format("flow_calibration: failed replacing %1%: %2%",
                                              src.string(), ec.message()));
        boost::filesystem::remove(tmp, ec);
    }

    BOOST_LOG_TRIVIAL(info) << "flow_calibration: scaled " << rewrites << " deposition moves across "
                            << id_to_factor.size() << " objects";
    return true;
}

} // namespace Slic3r
