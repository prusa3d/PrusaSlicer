///|/ Copyright (c) Prusa Research 2017 - 2021 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ ported from lib/Slic3r/GCode/SpiralVase.pm:
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SpiralVase.hpp"

#include <utility>
#include <cstddef>

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

static AABBTreeLines::LinesDistancer<Linef> get_layer_distancer(const std::vector<Vec2f> &layer_points)
{
    Linesf lines;
    for (size_t idx = 1; idx < layer_points.size(); ++idx)
        lines.emplace_back(layer_points[idx - 1].cast<double>(), layer_points[idx].cast<double>());

    return AABBTreeLines::LinesDistancer{std::move(lines)};
}

std::string SpiralVase::process_layer(const std::string &gcode, bool last_layer)
{
    /*  This post-processor relies on several assumptions:
        - all layers are processed through it, including those that are not supposed
          to be transformed, in order to update the reader with the XY positions
        - each call to this method includes a full layer, with a single Z move
          at the beginning
        - each layer is composed by suitable geometry (i.e. a single complete loop)
        - loops were not clipped before calling this method  */

    // If we're not going to modify G-code, just feed it to the reader
    // in order to update positions.
    if (!m_enabled) {
        m_reader.parse_buffer(gcode);
        return gcode;
    }

    // Continuous constant-wall spiral: the layer is one morphed multi-ring spiral path; keep it
    // FLAT at its nominal height and only ramp the final short arc (the in-wall elevator) up to
    // the next layer.
    if (m_config.constant_wall_spiral.value)
        return this->process_layer_multiwall(gcode, last_layer);

    // Get total XY length for this layer by summing all extrusion moves.
    float total_layer_length = 0.f;
    float layer_height       = 0.f;
    float z                  = 0.f;

    {
        //FIXME Performance warning: This copies the GCodeConfig of the reader.
        GCodeReader r = m_reader;  // clone
        bool set_z = false;
        r.parse_buffer(gcode, [&total_layer_length, &layer_height, &z, &set_z]
            (GCodeReader &reader, const GCodeReader::GCodeLine &line) {
            if (line.cmd_is("G1")) {
                if (line.extruding(reader)) {
                    total_layer_length += line.dist_XY(reader);
                } else if (line.has(Z)) {
                    layer_height += line.dist_Z(reader);
                    if (!set_z) {
                        z = line.new_Z(reader);
                        set_z = true;
                    }
                }
            }
        });
    }

    // Remove layer height from initial Z.
    z -= layer_height;

    // FIXME Tapering of the transition layer and smoothing only works reliably with relative extruder distances.
    // For absolute extruder distances it will be switched off.
    // Tapering the absolute extruder distances requires to process every extrusion value after the first transition
    // layer.
    const bool transition_in  = m_transition_layer && m_config.use_relative_e_distances.value;
    const bool transition_out = last_layer && m_config.use_relative_e_distances.value;
    const bool smooth_spiral  = m_smooth_spiral && m_config.use_relative_e_distances.value;

    const AABBTreeLines::LinesDistancer previous_layer_distancer = get_layer_distancer(m_previous_layer);
    Vec2f                               last_point               = m_previous_layer.empty() ? Vec2f::Zero() : m_previous_layer.back();
    float                               len                      = 0.f;

    std::string        new_gcode, transition_gcode;
    std::vector<Vec2f> current_layer;
    m_reader.parse_buffer(gcode, [z, total_layer_length, layer_height, transition_in, transition_out, smooth_spiral, max_xy_smoothing = m_max_xy_smoothing,
                                  &len, &last_point, &new_gcode, &transition_gcode, &current_layer, &previous_layer_distancer]
        (GCodeReader &reader, GCodeReader::GCodeLine line) {
        if (line.cmd_is("G1")) {
            if (line.has_z()) {
                // If this is the initial Z move of the layer, replace it with a
                // (redundant) move to the last Z of previous layer.
                line.set(reader, Z, z);
                new_gcode += line.raw() + '\n';
                return;
            } else if (line.has_x() || line.has_y()) { // Sometimes lines have X/Y but the move is to the last position.
                if (const float dist_XY = line.dist_XY(reader); dist_XY > 0 && line.extruding(reader)) { // Exclude wipe and retract
                    len += dist_XY;
                    const float factor = len / total_layer_length;
                    if (transition_in)
                        // Transition layer, interpolate the amount of extrusion from zero to the final value.
                        line.set(reader, E, line.e() * factor, 5);
                    else if (transition_out) {
                        // We want the last layer to ramp down extrusion, but without changing z height!
                        // So clone the line before we mess with its Z and duplicate it into a new layer that ramps down E
                        // We add this new layer at the very end
                        GCodeReader::GCodeLine transition_line(line);
                        transition_line.set(reader, E, line.e() * (1.f - factor), 5);
                        transition_gcode += transition_line.raw() + '\n';
                    }

                    // This line is the core of Spiral Vase mode, ramp up the Z smoothly
                    line.set(reader, Z, z + factor * layer_height);

                    bool emit_gcode_line = true;
                    if (smooth_spiral) {
                        // Now we also need to try to interpolate X and Y
                        Vec2f p(line.x(), line.y());   // Get current x/y coordinates
                        current_layer.emplace_back(p); // Store that point for later use on the next layer

                        auto [nearest_distance, idx, nearest_pt] = previous_layer_distancer.distance_from_lines_extra<false>(p.cast<double>());
                        if (nearest_distance < max_xy_smoothing) {
                            // Interpolate between the point on this layer and the point on the previous layer
                            Vec2f target = nearest_pt.cast<float>() * (1.f - factor) + p * factor;

                            // We will emit a new g-code line only when XYZ positions differ from the previous g-code line.
                            emit_gcode_line = GCodeFormatter::quantize(last_point) != GCodeFormatter::quantize(target);

                            line.set(reader, X, target.x());
                            line.set(reader, Y, target.y());
                            // We need to figure out the distance of this new line!
                            float modified_dist_XY = (last_point - target).norm();
                            // Scale the extrusion amount according to change in length
                            line.set(reader, E, line.e() * modified_dist_XY / dist_XY, 5);
                            last_point = target;
                        } else {
                            last_point = p;
                        }
                    }

                    if (emit_gcode_line)
                        new_gcode += line.raw() + '\n';
                }
                return;
                /*  Skip travel moves: the move to first perimeter point will
                    cause a visible seam when loops are not aligned in XY; by skipping
                    it we blend the first loop move in the XY plane (although the smoothness
                    of such blend depend on how long the first segment is; maybe we should
                    enforce some minimum length?).
                    When smooth_spiral is enabled, we're gonna end up exactly where the next layer should
                    start anyway, so we don't need the travel move */
            }
        }

        new_gcode += line.raw() + '\n';
        if (transition_out)
            transition_gcode += line.raw() + '\n';
    });

    m_previous_layer = std::move(current_layer);
    return new_gcode + transition_gcode;
}

// Multi-wall (thick) spiral vase - flat layers with a short spiral transition.
// The layer's perimeter G-code is one continuous morphed spiral covering every concentric wall.
// The whole layer is printed FLAT at its nominal Z, so every wall is present at every height ->
// a genuinely solid thick wall. Only the final L_TRANS of the layer (the tail of the last ring,
// which the perimeter morph alternates between the innermost and outermost wall per layer) ramps
// Z up by one layer height, turning the layer-change Z seam into a short fixed-length spiral.
// L_TRANS is a constant arc length (0.05 inch) regardless of part size.
std::string SpiralVase::process_layer_multiwall(const std::string &gcode, bool last_layer)
{
    // --- First pass: total XY extrusion length, layer height, and the layer's nominal Z. ---
    float total_len = 0.f, layer_height = 0.f, z_nominal = 0.f;
    bool  set_z = false;
    {
        GCodeReader r = m_reader; // clone
        r.parse_buffer(gcode, [&total_len, &layer_height, &z_nominal, &set_z]
            (GCodeReader &reader, const GCodeReader::GCodeLine &line) {
            if (line.cmd_is("G1")) {
                if (line.extruding(reader))
                    total_len += line.dist_XY(reader);
                else if (line.has(Z)) {
                    layer_height += line.dist_Z(reader);
                    if (! set_z) { z_nominal = line.new_Z(reader); set_z = true; }
                }
            }
        });
    }
    if (total_len <= 0.f) { m_reader.parse_buffer(gcode); return gcode; }

    // The whole layer prints FLAT at its nominal Z. The climb to the next layer is a single
    // STRAIGHT VERTICAL extruding move (the elevator column) appended after the layer's last
    // extrusion - the layer ends on the seam interstice pad the generator placed there, and the
    // next layer's paths arc around the column. The print's last layer gets no climb.
    std::vector<std::string> out_lines;
    size_t      last_extruding = std::string::npos;
    float       last_abs_e = 0.f;
    const bool  relative_e = m_config.use_relative_e_distances.value;
    m_reader.parse_buffer(gcode, [&]
        (GCodeReader &reader, GCodeReader::GCodeLine line) {
        if (! line.cmd_is("G1")) { out_lines.emplace_back(line.raw()); return; }
        // Flatten any Z move to the layer's nominal height; the vertical elevator at the end of
        // the previous layer already climbed us here.
        if (line.has_z() && ! (line.has_x() || line.has_y())) {
            line.set(reader, Z, z_nominal);
            out_lines.emplace_back(line.raw());
            return;
        }
        if (line.has(E) && ! relative_e)
            last_abs_e = line.e();
        if (line.extruding(reader) && line.dist_XY(reader) > 0) {
            line.set(reader, Z, z_nominal);
            out_lines.emplace_back(line.raw());
            last_extruding = out_lines.size() - 1;
        } else
            out_lines.emplace_back(line.raw());
    });

    if (! last_layer && layer_height > 0.f && last_extruding != std::string::npos) {
        // The vertical elevator: climb one layer height IN PLACE, right at the elevator pad (the
        // layer's last extrusion), extruding enough to leave a nozzle-diameter column bead
        // (pressure stays up, no retract, no restart). Inserted before any trailing travel so
        // the column lands exactly where the next layer's avoidance arcs expect it.
        const double max_nozzle    = *std::max_element(m_config.nozzle_diameter.values.begin(), m_config.nozzle_diameter.values.end());
        const double fil_d         = m_config.filament_diameter.get_at(0);
        const double filament_area = PI * 0.25 * fil_d * fil_d;
        const double col_volume    = PI * 0.25 * max_nozzle * max_nozzle * (double) layer_height;
        const double de            = col_volume / filament_area;
        char buf[96];
        if (relative_e)
            snprintf(buf, sizeof(buf), "G1 Z%.3f E%.5f F600", z_nominal + layer_height, de);
        else
            snprintf(buf, sizeof(buf), "G1 Z%.3f E%.5f F600", z_nominal + layer_height, last_abs_e + de);
        out_lines.insert(out_lines.begin() + last_extruding + 1, buf);
    }
    std::string new_gcode;
    new_gcode.reserve(gcode.size() + 32);
    for (const std::string &l : out_lines) {
        new_gcode += l;
        new_gcode += '\n';
    }
    return new_gcode;
}

}
