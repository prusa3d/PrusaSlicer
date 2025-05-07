///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FillEnsuring.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <utility>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <queue>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/Algorithm/PathSorting.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Surface.hpp"

namespace Slic3r {

const constexpr coord_t MAX_LINE_LENGTH_TO_FILTER   = scaled<coord_t>(4.); // 4 mm.
const constexpr size_t  MAX_SKIPS_ALLOWED           = 2; // Skip means propagation through long line.
const constexpr size_t  MIN_DEPTH_FOR_LINE_REMOVING = 5;

struct LineNode
{
    struct State
    {
        // The total number of long lines visited before this node was reached.
        // We just need the minimum number of all possible paths to decide whether we can remove the line or not.
        int min_skips_taken             = 0;
        // The total number of short lines visited before this node was reached.
        int total_short_lines           = 0;
        // Some initial line is touching some long line. This information is propagated to neighbors.
        bool initial_touches_long_lines = false;
        bool initialized                = false;

        void reset() {
            this->min_skips_taken            = 0;
            this->total_short_lines          = 0;
            this->initial_touches_long_lines = false;
            this->initialized                = false;
        }
    };

    explicit LineNode(const Line &line) : line(line) {}

    Line                   line;
    // Pointers to line nodes in the previous and the next section that overlap with this line.
    std::vector<LineNode*> next_section_overlapping_lines;
    std::vector<LineNode*> prev_section_overlapping_lines;

    bool                   is_removed = false;

    State                  state;

    // Return true if some initial line is touching some long line and this information was propagated into the current line.
    bool is_initial_line_touching_long_lines() const {
        if (prev_section_overlapping_lines.empty())
            return false;

        for (LineNode *line_node : prev_section_overlapping_lines) {
            if (line_node->state.initial_touches_long_lines)
                return true;
        }

        return false;
    }

    // Return true if the current line overlaps with some long line in the previous section.
    bool is_touching_long_lines_in_previous_layer() const {
        if (prev_section_overlapping_lines.empty())
            return false;

        for (LineNode *line_node : prev_section_overlapping_lines) {
            if (!line_node->is_removed && line_node->line.length() >= MAX_LINE_LENGTH_TO_FILTER)
                return true;
        }

        return false;
    }

    // Return true if the current line overlaps with some line in the next section.
    bool has_next_layer_neighbours() const {
        if (next_section_overlapping_lines.empty())
            return false;

        for (LineNode *line_node : next_section_overlapping_lines) {
            if (!line_node->is_removed)
                return true;
        }

        return false;
    }
};

using LineNodes = std::vector<LineNode>;

inline bool are_lines_overlapping_in_y_axes(const Line &first_line, const Line &second_line) {
    return (second_line.a.y() <= first_line.a.y() && first_line.a.y() <= second_line.b.y())
        || (second_line.a.y() <= first_line.b.y() && first_line.b.y() <= second_line.b.y())
        || (first_line.a.y() <= second_line.a.y() && second_line.a.y() <= first_line.b.y())
        || (first_line.a.y() <= second_line.b.y() && second_line.b.y() <= first_line.b.y());
}

bool can_line_note_be_removed(const LineNode &line_node) {
    return (line_node.line.length() < MAX_LINE_LENGTH_TO_FILTER)
        && (line_node.state.total_short_lines > int(MIN_DEPTH_FOR_LINE_REMOVING)
            || (!line_node.is_initial_line_touching_long_lines() && !line_node.has_next_layer_neighbours()));
}

// Remove the node and propagate its removal to the previous sections.
void propagate_line_node_remove(const LineNode &line_node) {
    std::queue<LineNode *> line_node_queue;
    for (LineNode *prev_line : line_node.prev_section_overlapping_lines) {
        if (prev_line->is_removed)
            continue;

        line_node_queue.emplace(prev_line);
    }

    for (; !line_node_queue.empty(); line_node_queue.pop()) {
        LineNode &line_to_check = *line_node_queue.front();

        if (can_line_note_be_removed(line_to_check)) {
            line_to_check.is_removed = true;

            for (LineNode *prev_line : line_to_check.prev_section_overlapping_lines) {
                if (prev_line->is_removed)
                    continue;

                line_node_queue.emplace(prev_line);
            }
        }
    }
}

// Filter out short extrusions that could create vibrations.
static std::vector<Lines> filter_vibrating_extrusions(const std::vector<Lines> &lines_sections) {
    // Initialize all line nodes.
    std::vector<LineNodes> line_nodes_sections(lines_sections.size());
    for (const Lines &lines_section : lines_sections) {
        const size_t section_idx = &lines_section - lines_sections.data();

        line_nodes_sections[section_idx].reserve(lines_section.size());
        for (const Line &line : lines_section) {
            line_nodes_sections[section_idx].emplace_back(line);
        }
    }

    // Precalculate for each line node which line nodes in the previous and next section this line node overlaps.
    for (auto curr_lines_section_it = line_nodes_sections.begin(); curr_lines_section_it != line_nodes_sections.end(); ++curr_lines_section_it) {
        if (curr_lines_section_it != line_nodes_sections.begin()) {
            const auto prev_lines_section_it = std::prev(curr_lines_section_it);
            for (LineNode &curr_line : *curr_lines_section_it) {
                for (LineNode &prev_line : *prev_lines_section_it) {
                    if (are_lines_overlapping_in_y_axes(curr_line.line, prev_line.line)) {
                        curr_line.prev_section_overlapping_lines.emplace_back(&prev_line);
                    }
                }
            }
        }

        if (std::next(curr_lines_section_it) != line_nodes_sections.end()) {
            const auto next_lines_section_it = std::next(curr_lines_section_it);
            for (LineNode &curr_line : *curr_lines_section_it) {
                for (LineNode &next_line : *next_lines_section_it) {
                    if (are_lines_overlapping_in_y_axes(curr_line.line, next_line.line)) {
                        curr_line.next_section_overlapping_lines.emplace_back(&next_line);
                    }
                }
            }
        }
    }

    // Select each section as the initial lines section and propagate line node states from this initial lines section to the last lines section.
    // During this propagation, we remove those lines that meet the conditions for its removal.
    // When some line is removed, we propagate this removal to previous layers.
    for (size_t initial_line_section_idx = 0; initial_line_section_idx < line_nodes_sections.size(); ++initial_line_section_idx) {
        // Stars from non-removed short lines.
        for (LineNode &initial_line : line_nodes_sections[initial_line_section_idx]) {
            if (initial_line.is_removed || initial_line.line.length() >= MAX_LINE_LENGTH_TO_FILTER)
                continue;

            initial_line.state.reset();
            initial_line.state.total_short_lines          = 1;
            initial_line.state.initial_touches_long_lines = initial_line.is_touching_long_lines_in_previous_layer();
            initial_line.state.initialized                = true;
        }

        // Iterate from the initial lines section until the last lines section.
        for (size_t propagation_line_section_idx = initial_line_section_idx; propagation_line_section_idx < line_nodes_sections.size(); ++propagation_line_section_idx) {
            // Before we propagate node states into next lines sections, we reset the state of all line nodes in the next line section.
            if (propagation_line_section_idx + 1 < line_nodes_sections.size()) {
                for (LineNode &propagation_line : line_nodes_sections[propagation_line_section_idx + 1]) {
                    propagation_line.state.reset();
                }
            }

            for (LineNode &propagation_line : line_nodes_sections[propagation_line_section_idx]) {
                if (propagation_line.is_removed || !propagation_line.state.initialized)
                    continue;

                for (LineNode *neighbour_line : propagation_line.next_section_overlapping_lines) {
                    if (neighbour_line->is_removed)
                        continue;

                    const bool is_short_line   = neighbour_line->line.length() < MAX_LINE_LENGTH_TO_FILTER;
                    const bool is_skip_allowed = propagation_line.state.min_skips_taken < int(MAX_SKIPS_ALLOWED);

                    if (!is_short_line && !is_skip_allowed)
                        continue;

                    const int neighbour_total_short_lines = propagation_line.state.total_short_lines + int(is_short_line);
                    const int neighbour_min_skips_taken   = propagation_line.state.min_skips_taken + int(!is_short_line);

                    if (neighbour_line->state.initialized) {
                        // When the state of the node was previously filled, then we need to update data in such a way
                        // that will maximize the possibility of removing this node.
                        neighbour_line->state.min_skips_taken = std::max(neighbour_line->state.min_skips_taken, neighbour_total_short_lines);
                        neighbour_line->state.min_skips_taken = std::min(neighbour_line->state.min_skips_taken, neighbour_min_skips_taken);

                        // We will keep updating neighbor initial_touches_long_lines until it is equal to false.
                        if (neighbour_line->state.initial_touches_long_lines) {
                            neighbour_line->state.initial_touches_long_lines = propagation_line.state.initial_touches_long_lines;
                        }
                    } else {
                        neighbour_line->state.total_short_lines          = neighbour_total_short_lines;
                        neighbour_line->state.min_skips_taken            = neighbour_min_skips_taken;
                        neighbour_line->state.initial_touches_long_lines = propagation_line.state.initial_touches_long_lines;
                        neighbour_line->state.initialized                = true;
                    }
                }

                if (can_line_note_be_removed(propagation_line)) {
                    // Remove the current node and propagate its removal to the previous sections.
                    propagation_line.is_removed = true;
                    propagate_line_node_remove(propagation_line);
                }
            }
        }
    }

    // Create lines sections without filtered-out lines.
    std::vector<Lines> lines_sections_out(line_nodes_sections.size());
    for (const std::vector<LineNode> &line_nodes_section : line_nodes_sections) {
        const size_t section_idx = &line_nodes_section - line_nodes_sections.data();

        for (const LineNode &line_node : line_nodes_section) {
            if (!line_node.is_removed) {
                lines_sections_out[section_idx].emplace_back(line_node.line);
            }
        }
    }

    return lines_sections_out;
}

ThickPolylines make_fill_polylines(
    const Fill *fill, const Surface *surface, const FillParams &params, bool stop_vibrations, bool fill_gaps, bool connect_extrusions)
{
    assert(fill->print_config != nullptr && fill->print_object_config != nullptr);

    auto rotate_thick_polylines = [](ThickPolylines &tpolylines, double cos_angle, double sin_angle) {
        for (ThickPolyline &tp : tpolylines) {
            for (auto &p : tp.points) {
                double px = double(p.x());
                double py = double(p.y());
                p.x()     = coord_t(round(cos_angle * px - sin_angle * py));
                p.y()     = coord_t(round(cos_angle * py + sin_angle * px));
            }
        }
    };

    const coord_t           scaled_spacing                      = scaled<coord_t>(fill->spacing);
    double                  distance_limit_reconnection         = 2.0 * double(scaled_spacing);
    double                  squared_distance_limit_reconnection = distance_limit_reconnection * distance_limit_reconnection;
    Polygons                filled_area                         = to_polygons(surface->expolygon);
    std::pair<float, Point> rotate_vector                       = fill->_infill_direction(surface);
    double                  aligning_angle                      = -rotate_vector.first + PI;
    polygons_rotate(filled_area, aligning_angle);
    BoundingBox bb = get_extents(filled_area);

    Polygons inner_area = stop_vibrations ? intersection(filled_area, opening(filled_area, 2 * scaled_spacing, 3 * scaled_spacing)) :
                                            filled_area;
    
    inner_area = shrink(inner_area, scaled_spacing * 0.5 - scaled<double>(fill->overlap));
    
    AABBTreeLines::LinesDistancer<Line> area_walls{to_lines(inner_area)};

    const size_t  n_vlines = (bb.max.x() - bb.min.x() + scaled_spacing - 1) / scaled_spacing;
    const coord_t y_min    = bb.min.y();
    const coord_t y_max    = bb.max.y();
    Lines         vertical_lines(n_vlines);

    for (size_t i = 0; i < n_vlines; i++) {
        coord_t x           = bb.min.x() + i * double(scaled_spacing);
        vertical_lines[i].a = Point{x, y_min};
        vertical_lines[i].b = Point{x, y_max};
    }

    if (!vertical_lines.empty()) {
        vertical_lines.push_back(vertical_lines.back());
        vertical_lines.back().a = Point{coord_t(bb.min.x() + n_vlines * double(scaled_spacing) + scaled_spacing * 0.5), y_min};
        vertical_lines.back().b = Point{vertical_lines.back().a.x(), y_max};
    }

    std::vector<Lines> polygon_sections(n_vlines);
    for (size_t i = 0; i < n_vlines; i++) {
        const auto intersections = area_walls.intersections_with_line<true>(vertical_lines[i]);

        for (int intersection_idx = 0; intersection_idx < int(intersections.size()) - 1; intersection_idx++) {
            const auto &a = intersections[intersection_idx];
            const auto &b = intersections[intersection_idx + 1];
            if (area_walls.outside((a.first + b.first) / 2) < 0) {
                if (std::abs(a.first.y() - b.first.y()) > scaled_spacing) {
                    polygon_sections[i].emplace_back(a.first, b.first);
                }
            }
        }
    }

    if (stop_vibrations) {
        polygon_sections = filter_vibrating_extrusions(polygon_sections);
    }

    ThickPolylines thick_polylines;
    {
        for (const auto &polygon_slice : polygon_sections) {
            for (const Line &segment : polygon_slice) {
                ThickPolyline &new_path = thick_polylines.emplace_back();
                new_path.points.push_back(segment.a);
                new_path.width.push_back(scaled_spacing);
                new_path.points.push_back(segment.b);
                new_path.width.push_back(scaled_spacing);
                new_path.endpoints = {true, true};
            }
        }
    }

    if (fill_gaps) {
        Polygons reconstructed_area{};
        // reconstruct polygon from polygon sections
        {
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<std::vector<Line>> polygon_sections_w_width = polygon_sections;
            for (auto &slice : polygon_sections_w_width) {
                for (Line &l : slice) {
                    l.a -= Point{0.0, 0.5 * scaled_spacing};
                    l.b += Point{0.0, 0.5 * scaled_spacing};
                }
            }

            std::vector<TracedPoly> current_traced_polys;
            for (const auto &polygon_slice : polygon_sections_w_width) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                                                             [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                                                             [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && !segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end()) {
                            continue;
                        }
                        if (connect_extrusions && (traced_poly.lows.back() - candidates_begin->a).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.lows.push_back(candidates_begin->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, 0});
                            traced_poly.lows.push_back(candidates_begin->a - Point{scaled_spacing / 2, 0});
                            traced_poly.lows.push_back(candidates_begin->a);
                        }

                        if (connect_extrusions && (traced_poly.highs.back() - candidates_begin->b).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.highs.push_back(candidates_begin->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b - Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidates_begin));
                    }

                    if (!segment_added) {
                        // Zero or multiple overlapping segments. Resolving this is nontrivial,
                        // so we just close this polygon and maybe open several new. This will hopefully happen much less often
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, 0});
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                        Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                                                          [](const TracedPoly &tp) { return tp.lows.empty(); }),
                                           current_traced_polys.end());

                for (const auto &segment : polygon_slice) {
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{scaled_spacing / 2, 0});
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{scaled_spacing / 2, 0});
                        new_tp.highs.push_back(segment.b);
                    }
                }
            }

            // add not closed polys
            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
        }

        reconstructed_area                     = union_safety_offset(reconstructed_area);
        ExPolygons gaps_for_additional_filling = diff_ex(filled_area, reconstructed_area);
        if (fill->overlap != 0) {
            gaps_for_additional_filling = offset_ex(gaps_for_additional_filling, scaled<float>(fill->overlap));
        }

        // BoundingBox bbox = get_extents(filled_area);
        // bbox.offset(scale_(1.));
        // ::Slic3r::SVG svg(debug_out_path(("surface" + std::to_string(surface->area())).c_str()).c_str(), bbox);
        // svg.draw(to_lines(filled_area), "red", scale_(0.4));
        // svg.draw(to_lines(reconstructed_area), "blue", scale_(0.3));
        // svg.draw(to_lines(gaps_for_additional_filling), "green", scale_(0.2));
        // svg.draw(vertical_lines, "black", scale_(0.1));
        // svg.Close();

        for (ExPolygon &ex_poly : gaps_for_additional_filling) {
            BoundingBox            ex_bb       = ex_poly.contour.bounding_box();
            coord_t                loops_count = (std::max(ex_bb.size().x(), ex_bb.size().y()) + scaled_spacing - 1) / scaled_spacing;
            Polygons               polygons    = to_polygons(ex_poly);
            Arachne::WallToolPaths wall_tool_paths(polygons, scaled_spacing, scaled_spacing, loops_count, 0, params.layer_height,
                                                   *fill->print_object_config, *fill->print_config);
            if (std::vector<Arachne::VariableWidthLines> loops = wall_tool_paths.getToolPaths(); !loops.empty()) {
                std::vector<const Arachne::ExtrusionLine *> all_extrusions;
                for (Arachne::VariableWidthLines &loop : loops) {
                    if (loop.empty())
                        continue;

                    for (const Arachne::ExtrusionLine &wall : loop)
                        all_extrusions.emplace_back(&wall);
                }

                for (const Arachne::ExtrusionLine *extrusion : all_extrusions) {
                    if (extrusion->junctions.size() < 2)
                        continue;

                    ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
                    if (extrusion->is_closed) {
                        // Arachne produces contour with clockwise orientation and holes with counterclockwise orientation.
                        if (const bool extrusion_reverse = params.prefer_clockwise_movements ? !extrusion->is_contour() : extrusion->is_contour(); extrusion_reverse)
                            thick_polyline.reverse();

                        thick_polyline.start_at_index(nearest_point_index(thick_polyline.points, ex_bb.min));
                        thick_polyline.clip_end(scaled_spacing * 0.5);
                    }

                    if (thick_polyline.is_valid() && thick_polyline.length() > 0 && thick_polyline.points.size() > 1) {
                        thick_polylines.push_back(thick_polyline);
                    }
                }
            }
        }

        std::sort(thick_polylines.begin(), thick_polylines.end(), [](const ThickPolyline &left, const ThickPolyline &right) {
            BoundingBox lbb(left.points);
            BoundingBox rbb(right.points);
            if (lbb.min.x() == rbb.min.x())
                return lbb.min.y() < rbb.min.y();
            else
                return lbb.min.x() < rbb.min.x();
        });

        // connect tiny gap fills to close colinear line
        struct EndPoint
        {
            Vec2d  position;
            size_t polyline_idx;
            size_t other_end_point_idx;
            bool   is_first;
            bool   used = false;
        };
        std::vector<EndPoint> connection_endpoints;
        connection_endpoints.reserve(thick_polylines.size() * 2);
        for (size_t pl_idx = 0; pl_idx < thick_polylines.size(); pl_idx++) {
            size_t current_idx = connection_endpoints.size();
            connection_endpoints.push_back({thick_polylines[pl_idx].first_point().cast<double>(), pl_idx, current_idx + 1, true});
            connection_endpoints.push_back({thick_polylines[pl_idx].last_point().cast<double>(), pl_idx, current_idx, false});
        }

        std::vector<bool> linear_segment_flags(thick_polylines.size());
        for (size_t i = 0;i < thick_polylines.size(); i++) {
            const ThickPolyline& tp = thick_polylines[i];
            linear_segment_flags[i] = tp.points.size() == 2 && tp.points.front().x() == tp.points.back().x() &&
                                      tp.width.front() == scaled_spacing && tp.width.back() == scaled_spacing;
        }

        auto coord_fn = [&connection_endpoints](size_t idx, size_t dim) { return connection_endpoints[idx].position[dim]; };
        KDTreeIndirect<2, double, decltype(coord_fn)> endpoints_tree{coord_fn, connection_endpoints.size()};
        for (size_t ep_idx = 0; ep_idx < connection_endpoints.size(); ep_idx++) {
            EndPoint &ep1 = connection_endpoints[ep_idx];
            if (!ep1.used) {
                std::vector<size_t> close_endpoints = find_nearby_points(endpoints_tree, ep1.position, double(scaled_spacing));
                for (size_t close_endpoint_idx : close_endpoints) {
                    EndPoint &ep2 = connection_endpoints[close_endpoint_idx];
                    if (ep2.used || ep2.polyline_idx == ep1.polyline_idx ||
                        (linear_segment_flags[ep1.polyline_idx] && linear_segment_flags[ep2.polyline_idx])) {
                        continue;
                    }

                    EndPoint &target_ep = ep1.polyline_idx > ep2.polyline_idx ? ep1 : ep2;
                    EndPoint &source_ep = ep1.polyline_idx > ep2.polyline_idx ? ep2 : ep1;

                    ThickPolyline &target_tp                     = thick_polylines[target_ep.polyline_idx];
                    ThickPolyline &source_tp                     = thick_polylines[source_ep.polyline_idx];
                    linear_segment_flags[target_ep.polyline_idx] = linear_segment_flags[ep1.polyline_idx] ||
                                                                   linear_segment_flags[ep2.polyline_idx];

                    Vec2d v1 = target_ep.is_first ?
                                   (target_tp.points[0] - target_tp.points[1]).cast<double>() :
                                   (target_tp.points.back() - target_tp.points[target_tp.points.size() - 1]).cast<double>();
                    Vec2d v2 = source_ep.is_first ?
                                   (source_tp.points[1] - source_tp.points[0]).cast<double>() :
                                   (source_tp.points[source_tp.points.size() - 1] - source_tp.points.back()).cast<double>();

                    if (std::abs(Slic3r::angle(v1, v2)) > PI / 6.0) {
                        continue;
                    }

                    // connect target_ep and source_ep, result is stored in target_tp, source_tp will be cleared
                    if (target_ep.is_first) {
                        target_tp.reverse();
                        target_ep.is_first                                           = false;
                        connection_endpoints[target_ep.other_end_point_idx].is_first = true;
                    }

                    size_t new_start_idx = target_ep.other_end_point_idx;

                    if (!source_ep.is_first) {
                        source_tp.reverse();
                        source_ep.is_first                                           = true;
                        connection_endpoints[source_ep.other_end_point_idx].is_first = false;
                    }

                    size_t new_end_idx = source_ep.other_end_point_idx;

                    target_tp.points.insert(target_tp.points.end(), source_tp.points.begin(), source_tp.points.end());
                    target_tp.width.push_back(target_tp.width.back());
                    target_tp.width.push_back(source_tp.width.front());
                    target_tp.width.insert(target_tp.width.end(), source_tp.width.begin(), source_tp.width.end());
                    target_ep.used = true;
                    source_ep.used = true;

                    connection_endpoints[new_start_idx].polyline_idx        = target_ep.polyline_idx;
                    connection_endpoints[new_end_idx].polyline_idx          = target_ep.polyline_idx;
                    connection_endpoints[new_start_idx].other_end_point_idx = new_end_idx;
                    connection_endpoints[new_end_idx].other_end_point_idx   = new_start_idx;
                    source_tp.clear();
                    break;
                }
            }
        }

        thick_polylines.erase(std::remove_if(thick_polylines.begin(), thick_polylines.end(),
                                             [scaled_spacing](const ThickPolyline &tp) {
                                                 return tp.length() < scaled_spacing &&
                                                        std::all_of(tp.width.begin(), tp.width.end(),
                                                                    [scaled_spacing](double w) { return w < scaled_spacing; });
                                             }),
                              thick_polylines.end());
    }

    Algorithm::sort_paths(thick_polylines.begin(), thick_polylines.end(), bb.min, double(scaled_spacing) * 1.2, [](const ThickPolyline &tp) {
        Lines ls;
        Point prev = tp.first_point();
        for (size_t i = 1; i < tp.points.size(); i++) {
            ls.emplace_back(prev, tp.points[i]);
            prev = ls.back().b;
        }
        return ls;
    });

    if (connect_extrusions) {
        ThickPolylines connected_thick_polylines;
        if (!thick_polylines.empty()) {
            connected_thick_polylines.push_back(thick_polylines.front());
            for (size_t tp_idx = 1; tp_idx < thick_polylines.size(); tp_idx++) {
                ThickPolyline &tp   = thick_polylines[tp_idx];
                ThickPolyline &tail = connected_thick_polylines.back();
                Point          last = tail.last_point();
                if ((last - tp.last_point()).cast<double>().squaredNorm() < (last - tp.first_point()).cast<double>().squaredNorm()) {
                    tp.reverse();
                }
                if ((last - tp.first_point()).cast<double>().squaredNorm() < squared_distance_limit_reconnection) {
                    tail.points.insert(tail.points.end(), tp.points.begin(), tp.points.end());
                    tail.width.push_back(scaled_spacing);
                    tail.width.push_back(scaled_spacing);
                    tail.width.insert(tail.width.end(), tp.width.begin(), tp.width.end());
                } else {
                    connected_thick_polylines.push_back(tp);
                }
            }
        }
        thick_polylines = connected_thick_polylines;
    }

    rotate_thick_polylines(thick_polylines, cos(-aligning_angle), sin(-aligning_angle));
    return thick_polylines;
}

} // namespace Slic3r
