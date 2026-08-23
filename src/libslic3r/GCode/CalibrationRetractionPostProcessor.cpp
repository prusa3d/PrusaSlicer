///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationRetractionPostProcessor.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/format.hpp"

#include <LocalesUtils.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r {

static constexpr const char *BUILTIN_PREFIX = "::builtin::retraction_calibration";

bool is_calibration_retraction_url(const std::string &script)
{
    return boost::starts_with(script, BUILTIN_PREFIX);
}

std::string make_calibration_retraction_url(double base_retract,
                                            const std::vector<std::pair<double, double>> &levels)
{
    // Force the C numeric locale so the URL always uses '.' decimals.
    // PrusaSlicer does not pin LC_NUMERIC globally, and the URL is
    // comma-delimited — a comma decimal separator would make it ambiguous.
    CNumericLocalesSetter locales_setter;

    std::ostringstream out;
    out << BUILTIN_PREFIX << "?base=";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", base_retract);
    out << buf << "&levels=";
    for (size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out << ',';
        std::snprintf(buf, sizeof(buf), "%.4f", levels[i].first);
        out << buf << ':';
        std::snprintf(buf, sizeof(buf), "%.4f", levels[i].second);
        out << buf;
    }
    return out.str();
}

namespace {

struct Params {
    double base_retract = 0.0;
    // (z_top_exclusive, retract_mm) sorted ascending by z_top.
    std::vector<std::pair<double, double>> levels;
};

// Throws on malformed URL.
Params parse_url(const std::string &url)
{
    auto qpos = url.find('?');
    if (qpos == std::string::npos)
        throw Slic3r::RuntimeError(format("retraction_calibration URL has no query string: %1%", url));

    Params p;
    bool   have_base = false, have_levels = false;

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

        if (key == "base") {
            p.base_retract = std::stod(val);
            have_base      = true;
        } else if (key == "levels") {
            size_t lp = 0;
            while (lp < val.size()) {
                size_t      comma = val.find(',', lp);
                std::string pair  = val.substr(lp, comma == std::string::npos ? std::string::npos : comma - lp);
                lp                = comma == std::string::npos ? val.size() : comma + 1;
                size_t colon      = pair.find(':');
                if (colon == std::string::npos)
                    throw Slic3r::RuntimeError(format("retraction_calibration level missing ':': %1%", pair));
                double z_top   = std::stod(pair.substr(0, colon));
                double retract = std::stod(pair.substr(colon + 1));
                p.levels.emplace_back(z_top, retract);
            }
            have_levels = true;
        }
    }

    if (!have_base || !have_levels || p.levels.empty())
        throw Slic3r::RuntimeError(format("retraction_calibration URL missing base/levels: %1%", url));

    // Defense: keep levels sorted by z_top ascending so retract_for_z scans cleanly.
    std::sort(p.levels.begin(), p.levels.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    return p;
}

double retract_for_z(double z, const Params &p)
{
    // Z below the first level (i.e. base layers) uses the printer's baseline.
    // Above the last level (shouldn't normally happen — tower height is bounded)
    // we clamp to the last test value.
    if (p.levels.empty() || z < 1.0 - 1e-9)
        return p.base_retract;
    for (const auto &lvl : p.levels)
        if (z < lvl.first - 1e-9)
            return lvl.second;
    return p.levels.back().second;
}

// Try to extract a number after a single-letter axis identifier. Returns true and
// writes the value into `out` on hit. Searches the substring before any ';' comment.
bool extract_axis(const std::string &line, char axis, double &out)
{
    size_t end = line.find(';');
    size_t i   = 0;
    while (i < (end == std::string::npos ? line.size() : end)) {
        if (line[i] == axis || line[i] == char(std::tolower(axis))) {
            // Make sure it's not part of an identifier (preceded by whitespace or start).
            if (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))) {
                size_t       j = i + 1;
                const size_t bound = end == std::string::npos ? line.size() : end;
                // Parse [+-]?[0-9.]+
                size_t start_num = j;
                if (j < bound && (line[j] == '+' || line[j] == '-'))
                    ++j;
                bool any = false;
                while (j < bound && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) {
                    any = true;
                    ++j;
                }
                if (any) {
                    out = std::stod(line.substr(start_num, j - start_num));
                    return true;
                }
            }
        }
        ++i;
    }
    return false;
}

// Returns true if the line is a "retract-only" G1 — has E but no X/Y/Z axis terms.
bool is_retract_only_g1(const std::string &line)
{
    if (line.size() < 3 || line[0] != 'G' || line[1] != '1')
        return false;
    if (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[2])))
        return false; // G10, G11, G17 etc.
    // Must have E.
    double dummy;
    if (!extract_axis(line, 'E', dummy))
        return false;
    // Must NOT have X, Y, or Z.
    if (extract_axis(line, 'X', dummy)) return false;
    if (extract_axis(line, 'Y', dummy)) return false;
    if (extract_axis(line, 'Z', dummy)) return false;
    return true;
}

// Returns 83 for an M83 line (relative E), 82 for M82 (absolute E), 0 otherwise.
// The trailing char after the number must be a separator so M820 etc. don't match.
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

} // namespace

bool run_calibration_retraction_post_processor(const std::string &url, const std::string &gcode_path)
{
    BOOST_LOG_TRIVIAL(info) << "retraction_calibration: starting in-process post-process on " << gcode_path;

    // Force the C numeric locale for the whole pass: parse_url() uses stod(),
    // the Z tracker uses stod(), and the rewritten G-code lines are formatted
    // with snprintf() — all of which must use '.' decimals regardless of the
    // user's system locale (PrusaSlicer does not pin LC_NUMERIC globally).
    CNumericLocalesSetter locales_setter;

    Params params = parse_url(url);

    boost::filesystem::path src(gcode_path);

    // Pass 1 — confirm this G-code is actually a retraction calibration print.
    // The `post_process` hook persists in the print preset, so this runs for
    // EVERY slice made with that preset. Only a G-code carrying the job-scoped
    // calibration marker should be rewritten; anything else is passed through
    // untouched. This also makes a normal .bgcode export a clean no-op (the
    // ASCII marker cannot appear in a binarized file) rather than an error.
    {
        boost::nowide::ifstream scan(gcode_path);
        if (!scan.is_open())
            throw Slic3r::RuntimeError(format("retraction_calibration: cannot open %1%", gcode_path));
        const std::string marker = calibration_retraction_marker();
        bool              found  = false;
        std::string       l;
        while (std::getline(scan, l)) {
            if (l.find(marker) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            BOOST_LOG_TRIVIAL(info) << "retraction_calibration: marker absent, leaving "
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
                "retraction_calibration: input %1% is binary G-code (.bgcode); "
                "the in-process rewriter only operates on ASCII. Set binary_gcode=false "
                "on the printer config for calibration prints.",
                gcode_path));
    }

    boost::filesystem::path tmp = src;
    tmp += ".retcal.tmp";

    boost::nowide::ifstream in(gcode_path);
    if (!in.is_open())
        throw Slic3r::RuntimeError(format("retraction_calibration: cannot open %1%", gcode_path));
    boost::nowide::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw Slic3r::RuntimeError(format("retraction_calibration: cannot write %1%", tmp.string()));

    static const std::regex z_comment(R"(^;Z:\s*([0-9.]+))");

    const std::string marker             = calibration_retraction_marker();
    double            current_z          = 0.0;
    double            last_retract       = 0.0;
    bool              has_pending_retract = false;
    // The retract/recovery classifier keys off the sign of E, which is only
    // meaningful in relative-E mode. The calibration dialog forces relative E
    // (use_relative_e_distances=true), so default to that; track M82/M83 in
    // case a hand-edited start G-code or a future regression flips it.
    bool              relative_e         = true;
    // Only retractions *between* the two markers (first layer / last layer)
    // belong to the tower body. Retractions before the first marker (start
    // G-code) and after the second (end G-code) are nozzle priming / cleanup
    // and must be left intact. The dialog emits exactly two markers; if only
    // one is present (e.g. an unexpectedly short print) the body simply runs
    // to EOF.
    bool              inside_body        = false;
    // The closing marker emits at the start of the last layer — between a
    // layer-change retract and its recovery. Dropping out of the body there
    // would rewrite the retract but leave the recovery at the sentinel value.
    // When the closing marker arrives mid-pair, defer the exit until that
    // recovery has been processed.
    bool              closing_after_recovery = false;
    size_t            rewrites           = 0;
    std::string       line;
    line.reserve(256);

    while (std::getline(in, line)) {
        // Strip trailing CR if present (Windows line endings).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Marker toggles the tower-body region.
        if (line.find(marker) != std::string::npos) {
            if (!inside_body)
                inside_body = true;                  // opening marker
            else if (has_pending_retract)
                closing_after_recovery = true;       // closing marker: finish the open pair first
            else
                inside_body = false;                 // closing marker: no open pair
            out << line << '\n';
            continue;
        }

        // Z tracking.
        std::smatch m;
        if (std::regex_search(line, m, z_comment)) {
            current_z = std::stod(m[1].str());
        } else {
            double z;
            if (extract_axis(line, 'Z', z))
                current_z = z;
        }

        // Extrusion-distance mode tracking.
        if (int mode = extrusion_mode_command(line); mode != 0)
            relative_e = (mode == 83);

        // Rewrite retract/recovery lines — but only inside the tower body, so
        // start/end-G-code priming retracts are left untouched.
        //
        // Retracts use the level for the current Z. Recoveries use the *same*
        // value as the matching prior retract — NOT a fresh lookup by Z. This
        // matters at band boundaries: the layer-change retract emits in band N
        // but its recovery lands at the start of layer N+1 where retract_for_z
        // would return band N+1's value. That mismatch would dump 0.1 mm of
        // extra plastic at every level-boundary seam (visible as tufts on the
        // inward face of the towers).
        if (inside_body && is_retract_only_g1(line)) {
            // Sign-based retract/recovery classification is only valid in
            // relative-E mode. In absolute-E mode a retract is a smaller
            // absolute coordinate (usually still positive) and would be
            // misread as a recovery — refuse rather than corrupt the print.
            if (!relative_e)
                throw Slic3r::RuntimeError(format(
                    "retraction_calibration: %1% uses absolute E distances (M82); "
                    "the rewriter requires relative E (M83). Set "
                    "use_relative_e_distances=true on the printer config.",
                    gcode_path));
            double e_val = 0.0;
            extract_axis(line, 'E', e_val);

            // Only rewrite genuine retract/recovery pairs:
            //  - a retract is a negative-E move;
            //  - a recovery is a positive-E move that follows a retract.
            // A standalone positive-E move with no pending retract is a
            // prime / unload / custom-G-code command (default profiles emit
            // e.g. `G1 E2` / `G1 E10`) and must be passed through untouched —
            // forcing it to a retraction value would corrupt those sequences.
            const bool is_retract  = e_val < 0.0;
            const bool is_recovery = e_val > 0.0 && has_pending_retract;
            if (is_retract || is_recovery) {
                double new_e;
                char   tag;
                if (is_retract) {
                    last_retract        = retract_for_z(current_z, params);
                    has_pending_retract = true;
                    new_e               = -last_retract;
                    tag                 = 'r'; // retract
                } else {
                    new_e               = last_retract;
                    has_pending_retract = false;
                    tag                 = 'R'; // recovery (deretract)
                }
                // Preserve feedrate if present.
                double f_val  = 0.0;
                bool   have_f = extract_axis(line, 'F', f_val);
                char   buf[128];
                if (have_f)
                    std::snprintf(buf, sizeof(buf), "G1 E%.4f F%g ; %c z=%.2f", new_e, f_val, tag, current_z);
                else
                    std::snprintf(buf, sizeof(buf), "G1 E%.4f ; %c z=%.2f", new_e, tag, current_z);
                line = buf;
                ++rewrites;
            }
        }

        // A closing marker arrived while a retract was still open; now that
        // its recovery has been emitted, leave the tower body.
        if (closing_after_recovery && !has_pending_retract) {
            inside_body            = false;
            closing_after_recovery = false;
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
            throw Slic3r::RuntimeError(format("retraction_calibration: failed replacing %1%: %2%",
                                              src.string(), ec.message()));
        boost::filesystem::remove(tmp, ec);
    }

    BOOST_LOG_TRIVIAL(info) << "retraction_calibration: rewrote " << rewrites << " retract/recovery lines";
    return true;
}

} // namespace Slic3r
