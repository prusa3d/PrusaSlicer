///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPALinePostProcessor.hpp"
#include "CalibrationPAPostProcessor.hpp"   // calibration_pa_marker()

#include "libslic3r/Exception.hpp"
#include "libslic3r/format.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

static constexpr const char *BUILTIN_PREFIX = "::builtin::pa_line_pattern";

bool is_pa_line_url(const std::string &script)
{
    return boost::starts_with(script, BUILTIN_PREFIX);
}

std::string make_pa_line_url(const std::string &body_file)
{
    return std::string(BUILTIN_PREFIX) + "?body=" + body_file;
}

namespace {

// Extract the body= value (throws on a malformed URL).
std::string parse_body_path(const std::string &url)
{
    auto qpos = url.find('?');
    if (qpos == std::string::npos)
        throw Slic3r::RuntimeError(format("pa_line URL has no query string: %1%", url));
    std::string query = url.substr(qpos + 1);
    // Single key=value (body=...); the value (a file path) may contain anything
    // except a leading "key=" — take everything after the first '='.
    size_t eq = query.find('=');
    if (eq == std::string::npos || query.substr(0, eq) != "body")
        throw Slic3r::RuntimeError(format("pa_line URL missing body=: %1%", url));
    std::string path = query.substr(eq + 1);
    if (path.empty())
        throw Slic3r::RuntimeError(format("pa_line URL has empty body path: %1%", url));
    return path;
}

} // namespace

bool run_pa_line_post_processor(const std::string &url, const std::string &gcode_path)
{
    BOOST_LOG_TRIVIAL(info) << "pa_line: starting in-process post-process on " << gcode_path;

    const std::string body_path = parse_body_path(url);
    const std::string marker    = calibration_pa_marker();

    // Pass 1 — only a marker-carrying calibration G-code is rewritten.
    {
        boost::nowide::ifstream scan(gcode_path);
        if (!scan.is_open())
            throw Slic3r::RuntimeError(format("pa_line: cannot open %1%", gcode_path));
        bool        found = false;
        std::string l;
        while (std::getline(scan, l)) {
            if (l.find(marker) != std::string::npos) { found = true; break; }
        }
        if (!found) {
            BOOST_LOG_TRIVIAL(info) << "pa_line: marker absent, leaving " << gcode_path << " unchanged";
            return false;
        }
    }

    // Marker-carrying files must be ASCII (the dialog forces binary_gcode=false).
    {
        boost::nowide::ifstream sniff(gcode_path, std::ios::binary);
        char magic[4] = {};
        sniff.read(magic, 4);
        if (sniff.gcount() == 4 && std::string(magic, 4) == "GCDE")
            throw Slic3r::RuntimeError(format(
                "pa_line: input %1% is binary G-code (.bgcode); the in-process rewriter "
                "only operates on ASCII. Set binary_gcode=false for calibration prints.",
                gcode_path));
    }

    // Load the generated toolpath body.
    std::string body;
    {
        boost::nowide::ifstream bf(body_path, std::ios::binary);
        if (!bf.is_open())
            throw Slic3r::RuntimeError(format(
                "pa_line: cannot read the generated toolpath body %1% — re-run the PA Line "
                "calibration to regenerate it (the body is a temporary file and is not kept "
                "when the project is saved/reopened or the temp directory is cleaned).",
                body_path));
        std::ostringstream ss;
        ss << bf.rdbuf();
        body = ss.str();
    }
    if (body.empty())
        throw Slic3r::RuntimeError(format("pa_line: toolpath body %1% is empty", body_path));

    static const std::string kStart = "; printing object ";
    static const std::string kStop  = "; stop printing object ";

    // Locate the placeholder's object body STRUCTURALLY, not by "the first label
    // after the marker".
    //
    // OctoPrint labeling lists every object twice: once in a header, where
    // all_objects_header() writes start_object() immediately followed by
    // stop_object() on the very next line, and once around the real body, where the
    // two are separated by the sliced extrusions. The body's opening label is
    // therefore the first "; printing object " that is NOT immediately followed by
    // "; stop printing object ".
    //
    // This deliberately does not key on the marker, because the slicer does not emit
    // the two in a fixed order. A single-tool print emits the body label after the
    // per-layer custom G-code that carries the marker; a multi-tool print — any print
    // whose highest used tool id is > 0, i.e. an INDX/XL/MMU with the filament in slot
    // 2 or later — emits it from change_layer(), BEFORE the marker. Gating on the
    // marker never found the body on a toolchanger and aborted the export (#49).
    size_t splice_start = std::string::npos;   // line index of the body's "; printing object"
    size_t splice_stop  = std::string::npos;   // line index of its "; stop printing object"
    size_t marker_line  = std::string::npos;
    {
        boost::nowide::ifstream idx(gcode_path);
        if (!idx.is_open())
            throw Slic3r::RuntimeError(format("pa_line: cannot open %1%", gcode_path));
        // Record only the label positions, so this stays O(labels) rather than O(file).
        std::vector<std::pair<size_t, bool>> labels;   // (line index, is_start)
        std::string l;
        for (size_t i = 0; std::getline(idx, l); ++ i) {
            if (!l.empty() && l.back() == '\r')
                l.pop_back();
            if (marker_line == std::string::npos && l.find(marker) != std::string::npos)
                marker_line = i;
            if (boost::starts_with(l, kStart))
                labels.emplace_back(i, true);
            else if (boost::starts_with(l, kStop))
                labels.emplace_back(i, false);
        }
        for (size_t k = 0; k < labels.size(); ++ k) {
            if (! labels[k].second)
                continue;
            const bool header_pair = k + 1 < labels.size() && ! labels[k + 1].second &&
                                     labels[k + 1].first == labels[k].first + 1;
            if (header_pair)
                continue;
            splice_start = labels[k].first;
            for (size_t m = k + 1; m < labels.size(); ++ m)
                if (! labels[m].second) { splice_stop = labels[m].first; break; }
            break;
        }
    }
    if (splice_start == std::string::npos)
        throw Slic3r::RuntimeError(format(
            "pa_line: no object body found in %1% to splice the generated toolpath into. "
            "Re-run Calibration -> Pressure Advance (Line) to re-apply the settings this "
            "export needs (it labels objects in the OctoPrint style so the body can be "
            "located).", gcode_path));
    if (splice_stop == std::string::npos)
        // The placeholder body was never closed by "; stop printing object", so the
        // end G-code (cooldown / steppers-off) would be dropped. Fail loudly rather
        // than write a truncated print.
        throw Slic3r::RuntimeError(format(
            "pa_line: object body in %1% had no closing \"; stop printing object\" — "
            "refusing to silently drop the end G-code", gcode_path));

    // The generated body arrives expecting a RETRACTED nozzle — it opens each section
    // with its own unretract. In the single-tool order that holds for free, because
    // change_layer()'s retract_and_wipe() runs before the label. In the multi-tool order
    // the label comes first and the retract lands after it (GCode.cpp:3060 then :3084),
    // so splicing straight after the label would drop that retract and the body's opening
    // unretract would blob an extra retract_length before the first anchor bar.
    //
    // The marker is emitted as first-layer custom G-code — after the layer-change setup
    // and before the placeholder's extrusions — in BOTH orders. So splice after whichever
    // of the label and the marker comes later: the setup between them is always kept, and
    // only the placeholder's own extrusions are replaced.
    if (marker_line != std::string::npos && marker_line > splice_start && marker_line < splice_stop)
        splice_start = marker_line;

    boost::filesystem::path src(gcode_path);
    boost::filesystem::path tmp = src;
    tmp += ".paline.tmp";

    boost::nowide::ifstream in(gcode_path);
    if (!in.is_open())
        throw Slic3r::RuntimeError(format("pa_line: cannot open %1%", gcode_path));
    boost::nowide::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw Slic3r::RuntimeError(format("pa_line: cannot write %1%", tmp.string()));

    // Keep everything outside [splice_start, splice_stop] verbatim — the OctoPrint
    // header, the start G-code, the layer-change setup and the end G-code — and replace
    // the placeholder's sliced extrusions with the generated toolpath.
    std::string line;
    for (size_t i = 0; std::getline(in, line); ++ i) {
        if (i <= splice_start || i >= splice_stop) {
            out << line << '\n';
            if (i == splice_start) {
                out << body;
                if (body.back() != '\n')
                    out << '\n';
            }
        }
        // else: inside the placeholder's sliced body — dropped
    }
    in.close();
    out.close();

    boost::system::error_code ec;
    boost::filesystem::rename(tmp, src, ec);
    if (ec) {
        boost::filesystem::copy_file(tmp, src, boost::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            throw Slic3r::RuntimeError(format("pa_line: failed replacing %1%: %2%",
                                              src.string(), ec.message()));
        boost::filesystem::remove(tmp, ec);
    }
    BOOST_LOG_TRIVIAL(info) << "pa_line: spliced generated toolpath into " << gcode_path;
    return true;
}

} // namespace Slic3r
