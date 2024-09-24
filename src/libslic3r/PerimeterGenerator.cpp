///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2021 Ilya @xorza
///|/ Copyright (c) Slic3r 2015 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PerimeterGenerator.hpp"

#include <ankerl/unordered_dense.h>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <tuple>

#include "AABBTreeLines.hpp"
#include "BoundingBox.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"
#include "ShortestPath.hpp"
#include "Surface.hpp"
#include "Geometry/ConvexHull.hpp"
#include "clipper/clipper_z.hpp"
#include "Arachne/PerimeterOrder.hpp"
#include "Arachne/WallToolPaths.hpp"
#include "Arachne/utils/ExtrusionLine.hpp"
#include "Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r.h"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Line.hpp"

//#define ARACHNE_DEBUG

#ifdef ARACHNE_DEBUG
#include "SVG.hpp"
#include "Utils.hpp"
#endif

namespace Slic3r {

ExtrusionMultiPath PerimeterGenerator::thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, const float tolerance, const float merge_tolerance)
{
    ExtrusionMultiPath multi_path;
    ExtrusionPath      path(role);
    ThickLines         lines = thick_polyline.thicklines();

    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];
        assert(line.a_width >= SCALED_EPSILON && line.b_width >= SCALED_EPSILON);

        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) {
            // The line is so tiny that we don't care about its width when we connect it to another line.
            if (!path.empty())
                path.polyline.points.back() = line.b; // If the variable path is non-empty, connect this tiny line to it.
            else if (i + 1 < (int)lines.size()) // If there is at least one following line, connect this tiny line to it.
                lines[i + 1].a = line.a;
            else if (!multi_path.paths.empty())
                multi_path.paths.back().polyline.points.back() = line.b; // Connect this tiny line to the last finished path.

            // If any of the above isn't satisfied, then remove this tiny line.
            continue;
        }

        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance) {
            const auto segments = (unsigned int)ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j) {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());

                    coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);

                assert(pp.size() == segments + 1u);
                assert(width.size() == segments*2);
            }

            // delete this line and insert new ones
            lines.erase(lines.begin() + i);
            for (size_t j = 0; j < segments; ++j) {
                ThickLine new_line(pp[j], pp[j+1]);
                new_line.a_width = width[2*j];
                new_line.b_width = width[2*j+1];
                lines.insert(lines.begin() + i + j, new_line);
            }

            -- i;
            continue;
        }

        const double w        = fmax(line.a_width, line.b_width);
        const Flow   new_flow = (role.is_bridge() && flow.bridge()) ? flow : flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
        if (path.empty()) {
            // Convert from spacing to extrusion width based on the extrusion model
            // of a square extrusion ended with semi circles.
            path = { ExtrusionAttributes{ path.role(), new_flow } };
            path.polyline.append(line.a);
            path.polyline.append(line.b);
            #ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
            #endif
        } else {
            assert(path.width() >= EPSILON);
            thickness_delta = scaled<double>(fabs(path.width() - new_flow.width()));
            if (thickness_delta <= merge_tolerance) {
                // the width difference between this line and the current flow
                // (of the previous line) width is within the accepted tolerance
                path.polyline.append(line.b);
            } else {
                // we need to initialize a new line
                multi_path.paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                -- i;
            }
        }
    }
    if (path.polyline.is_valid())
        multi_path.paths.emplace_back(std::move(path));
    return multi_path;
}

static void variable_width_classic(const ThickPolylines &polylines, ExtrusionRole role, const Flow &flow, std::vector<ExtrusionEntity *> &out)
{
    // This value determines granularity of adaptive width, as G-code does not allow
    // variable extrusion within a single move; this value shall only affect the amount
    // of segments, and any pruning shall be performed before we apply this tolerance.
    const auto tolerance = float(scale_(0.05));
    for (const ThickPolyline &p : polylines) {
        ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(p, role, flow, tolerance, tolerance);
        // Append paths to collection.
        if (!multi_path.paths.empty()) {
            for (auto it = std::next(multi_path.paths.begin()); it != multi_path.paths.end(); ++it) {
                assert(it->polyline.points.size() >= 2);
                assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
            }

            if (multi_path.paths.front().first_point() == multi_path.paths.back().last_point())
                out.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
            else
                out.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
        }
    }
}

// Hierarchy of perimeters.
class PerimeterGeneratorLoop {
public:
    // Polygon of this contour.
    Polygon                             polygon;
    // Is it a contour or a hole?
    // Contours are CCW oriented, holes are CW oriented.
    bool                                is_contour;
    // Depth in the hierarchy. External perimeter has depth = 0. An external perimeter could be both a contour and a hole.
    unsigned short                      depth;
    // Should this contur be fuzzyfied on path generation?
    bool                                fuzzify;
    // Children contour, may be both CCW and CW oriented (outer contours or holes).
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(const Polygon &polygon, unsigned short depth, bool is_contour, bool fuzzify) : 
        polygon(polygon), is_contour(is_contour), depth(depth), fuzzify(fuzzify) {}
    // External perimeter. It may be CCW or CW oriented (outer contour or hole contour).
    bool is_external() const { return this->depth == 0; }
    // An island, which may have holes, but it does not have another internal island.
    bool is_internal_contour() const {
        // An internal contour is a contour containing no other contours
        if (! this->is_contour)
            return false;
        for (const PerimeterGeneratorLoop &loop : this->children)
            if (loop.is_contour)
                return false;
        return true;
    }
};

// Thanks Cura developers for this function.
static void fuzzy_polygon(Polygon &poly, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double dist_left_over = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point
    Point* p0 = &poly.points.back();
    Points out;
    out.reserve(poly.points.size());
    for (Point &p1 : poly.points)
    { // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
            p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX))
        {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly.points = std::move(out);
}

// Thanks Cura developers for this function.
static void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double       dist_left_over          = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point

    auto                                   *p0 = &ext_lines.front();
    std::vector<Arachne::ExtrusionJunction> out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines) {
        if (p0->p == p1.p) { // Connect endpoints.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX)) {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w, p1.perimeter_index);
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        -- point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) // Connect endpoints.
        out.front().p = out.back().p;

    if (out.size() >= 3)
        ext_lines.junctions = std::move(out);
}

using PerimeterGeneratorLoops = std::vector<PerimeterGeneratorLoop>;

static ExtrusionEntityCollection traverse_loops_classic(const PerimeterGenerator::Parameters &params, const Polygons &lower_slices_polygons_cache, const PerimeterGeneratorLoops &loops, ThickPolylines &thin_walls)
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection   coll;
    Polygon                     fuzzified;
    for (const PerimeterGeneratorLoop &loop : loops) {
        bool is_external = loop.is_external();
        
        ExtrusionLoopRole loop_role;
        ExtrusionRole role_normal   = is_external ? ExtrusionRole::ExternalPerimeter : ExtrusionRole::Perimeter;
        ExtrusionRole role_overhang = role_normal | ExtrusionRoleModifier::Bridge;
        if (loop.is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;
        const Polygon &polygon = loop.fuzzify ? fuzzified : loop.polygon;
        if (loop.fuzzify) {
            fuzzified = loop.polygon;
            fuzzy_polygon(fuzzified, scaled<float>(params.config.fuzzy_skin_thickness.value), scaled<float>(params.config.fuzzy_skin_point_dist.value));
        }
        if (params.config.overhangs && params.layer_id > params.object_config.raft_layers
            && ! ((params.object_config.support_material || params.object_config.support_material_enforce_layers > 0) && 
                  params.object_config.support_material_contact_distance.value == 0)) {
            BoundingBox bbox(polygon.points);
            bbox.offset(SCALED_EPSILON);
            Polygons lower_slices_polygons_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_slices_polygons_cache, bbox);
            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(
                paths,
                intersection_pl({ polygon }, lower_slices_polygons_clipped),
                ExtrusionAttributes{
                    role_normal,
                    ExtrusionFlow{ is_external ? params.ext_mm3_per_mm : params.mm3_per_mm,
                                   is_external ? params.ext_perimeter_flow.width() : params.perimeter_flow.width(),
                                   float(params.layer_height)
                } });
            
            // get overhang paths by checking what parts of this loop fall 
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(
                paths,
                diff_pl({ polygon }, lower_slices_polygons_clipped),
                ExtrusionAttributes{
                    role_overhang,
                    ExtrusionFlow{ params.mm3_per_mm_overhang, params.overhang_flow.width(), params.overhang_flow.height() }
                });
            
            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            chain_and_reorder_extrusion_paths(paths, &paths.front().first_point());
        } else {
            paths.emplace_back(polygon.split_at_first_point(),
                ExtrusionAttributes{
                    role_normal,
                    ExtrusionFlow{
                        is_external ? params.ext_mm3_per_mm : params.mm3_per_mm,
                        is_external ? params.ext_perimeter_flow.width() : params.perimeter_flow.width(),
                        float(params.layer_height)
                    }
                });
        }
       
        coll.append(ExtrusionLoop(std::move(paths), loop_role));
    }
    
    // Append thin walls to the nearest-neighbor search (only for first iteration)
    if (! thin_walls.empty()) {
        variable_width_classic(thin_walls, ExtrusionRole::ExternalPerimeter, params.ext_perimeter_flow, coll.entities);
        thin_walls.clear();
    }
    
    // Traverse children and build the final collection.
	Point zero_point(0, 0);
	std::vector<std::pair<size_t, bool>> chain = chain_extrusion_entities(coll.entities, &zero_point);
    ExtrusionEntityCollection out;
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(coll.entities[idx.first] != nullptr);
        if (idx.first >= loops.size()) {
            // This is a thin wall.
			out.entities.reserve(out.entities.size() + 1);
            out.entities.emplace_back(coll.entities[idx.first]);
			coll.entities[idx.first] = nullptr;
            if (idx.second)
				out.entities.back()->reverse();
        } else {
            const PerimeterGeneratorLoop &loop = loops[idx.first];
            assert(thin_walls.empty());
            ExtrusionEntityCollection children = traverse_loops_classic(params, lower_slices_polygons_cache, loop.children, thin_walls);
            out.entities.reserve(out.entities.size() + children.entities.size() + 1);
            ExtrusionLoop *eloop = static_cast<ExtrusionLoop*>(coll.entities[idx.first]);
            coll.entities[idx.first] = nullptr;
            if (loop.is_contour) {
                if (eloop->is_clockwise())
                    eloop->reverse_loop();
                out.append(std::move(children.entities));
                out.entities.emplace_back(eloop);
            } else {
                if (eloop->is_counter_clockwise())
                    eloop->reverse_loop();
                out.entities.emplace_back(eloop);
                out.append(std::move(children.entities));
            }
        }
    }
    return out;
}

static ClipperLib_Z::Paths clip_extrusion(const ClipperLib_Z::Path &subject, const ClipperLib_Z::Paths &clip, ClipperLib_Z::ClipType clipType)
{
    ClipperLib_Z::Clipper clipper;
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint &e1bot, const ClipperLib_Z::IntPoint &e1top, const ClipperLib_Z::IntPoint &e2bot,
                             const ClipperLib_Z::IntPoint &e2top, ClipperLib_Z::IntPoint &pt) {
        // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
        // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are 
        // harmless to the result of the clipping operation,
        // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
        assert(e1bot.z() >= 0 && e1top.z() >= 0);
        assert(e2bot.z() >= 0 && e2top.z() >= 0);
        assert((e1bot.z() == 0) == (e1top.z() == 0));
        assert((e2bot.z() == 0) == (e2top.z() == 0));

        // Start & end points of the clipped polyline (extrusion path with a non-zero width).
        ClipperLib_Z::IntPoint start = e1bot;
        ClipperLib_Z::IntPoint end   = e1top;
        if (start.z() <= 0 && end.z() <= 0) {
            start = e2bot;
            end   = e2top;
        }

        if (start.z() <= 0 && end.z() <= 0) {
            // Self intersection on the source contour.
            assert(start.z() == 0 && end.z() == 0);
            pt.z() = 0;
        } else {
            // Interpolate extrusion line width.
            assert(start.z() > 0 && end.z() > 0);

            double length_sqr = (end - start).cast<double>().squaredNorm();
            double dist_sqr   = (pt - start).cast<double>().squaredNorm();
            double t          = std::sqrt(dist_sqr / length_sqr);

            pt.z() = start.z() + coord_t((end.z() - start.z()) * t);
        }
    });

    clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPaths(clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::Paths    clipped_paths;
    {
        ClipperLib_Z::PolyTree clipped_polytree;
        clipper.Execute(clipType, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(std::move(clipped_polytree), clipped_paths);
    }

    // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
    // For those vertices, we must assign value based on the subject.
    // This happens only in sporadic cases.
    for (ClipperLib_Z::Path &path : clipped_paths)
        for (ClipperLib_Z::IntPoint &c_pt : path)
            if (c_pt.z() == 0) {
                // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                if (subject.size() <= 2)
                    continue;

                const Point pt(c_pt.x(), c_pt.y());
                Point       projected_pt_min;
                auto        it_min       = subject.begin();
                auto        dist_sqr_min = std::numeric_limits<double>::max();
                Point       prev(subject.front().x(), subject.front().y());
                for (auto it = std::next(subject.begin()); it != subject.end(); ++it) {
                    Point curr(it->x(), it->y());
                    Point projected_pt;
                    if (double dist_sqr = line_alg::distance_to_squared(Line(prev, curr), pt, &projected_pt); dist_sqr < dist_sqr_min) {
                        dist_sqr_min     = dist_sqr;
                        projected_pt_min = projected_pt;
                        it_min           = std::prev(it);
                    }
                    prev = curr;
                }

                assert(dist_sqr_min <= SCALED_EPSILON);
                assert(std::next(it_min) != subject.end());

                const Point  pt_a(it_min->x(), it_min->y());
                const Point  pt_b(std::next(it_min)->x(), std::next(it_min)->y());
                const double line_len = (pt_b - pt_a).cast<double>().norm();
                const double dist     = (projected_pt_min - pt_a).cast<double>().norm();
                c_pt.z()              = coord_t(double(it_min->z()) + (dist / line_len) * double(std::next(it_min)->z() - it_min->z()));
            }

    assert([&clipped_paths = std::as_const(clipped_paths)]() -> bool {
        for (const ClipperLib_Z::Path &path : clipped_paths)
            for (const ClipperLib_Z::IntPoint &pt : path)
                if (pt.z() <= 0)
                    return false;
        return true;
    }());

    return clipped_paths;
}

static ExtrusionEntityCollection traverse_extrusions(const PerimeterGenerator::Parameters &params, const Polygons &lower_slices_polygons_cache, Arachne::PerimeterOrder::PerimeterExtrusions &pg_extrusions)
{
    ExtrusionEntityCollection extrusion_coll;
    for (Arachne::PerimeterOrder::PerimeterExtrusion &pg_extrusion : pg_extrusions) {
        Arachne::ExtrusionLine &extrusion = pg_extrusion.extrusion;
        if (extrusion.empty())
            continue;

        const bool    is_external   = extrusion.inset_idx == 0;
        ExtrusionRole role_normal   = is_external ? ExtrusionRole::ExternalPerimeter : ExtrusionRole::Perimeter;
        ExtrusionRole role_overhang = role_normal | ExtrusionRoleModifier::Bridge;

        if (pg_extrusion.fuzzify)
            fuzzy_extrusion_line(extrusion, scaled<float>(params.config.fuzzy_skin_thickness.value), scaled<float>(params.config.fuzzy_skin_point_dist.value));

        ExtrusionPaths paths;
        // detect overhanging/bridging perimeters
        if (params.config.overhangs && params.layer_id > params.object_config.raft_layers
            && ! ((params.object_config.support_material || params.object_config.support_material_enforce_layers > 0) &&
                 params.object_config.support_material_contact_distance.value == 0)) {

            ClipperLib_Z::Path extrusion_path;
            extrusion_path.reserve(extrusion.size());
            BoundingBox extrusion_path_bbox;
            for (const Arachne::ExtrusionJunction &ej : extrusion.junctions) {
                extrusion_path.emplace_back(ej.p.x(), ej.p.y(), ej.w);
                extrusion_path_bbox.merge(Point{ej.p.x(), ej.p.y()});
            }

            ClipperLib_Z::Paths lower_slices_paths;
            lower_slices_paths.reserve(lower_slices_polygons_cache.size());
            {
                Points clipped;
                extrusion_path_bbox.offset(SCALED_EPSILON);
                for (const Polygon &poly : lower_slices_polygons_cache) {
                    clipped.clear();
                    ClipperUtils::clip_clipper_polygon_with_subject_bbox(poly.points, extrusion_path_bbox, clipped);
                    if (! clipped.empty()) {
                        lower_slices_paths.emplace_back();
                        ClipperLib_Z::Path &out = lower_slices_paths.back();
                        out.reserve(clipped.size());
                        for (const Point &pt : clipped)
                            out.emplace_back(pt.x(), pt.y(), 0);
                    }
                }
            }

            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctIntersection), role_normal,
                                   is_external ? params.ext_perimeter_flow : params.perimeter_flow);

            // get overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctDifference), role_overhang,
                                   params.overhang_flow);

            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            // Arachne sometimes creates extrusion with zero-length (just two same endpoints);
            if (!paths.empty()) {
                Point start_point = paths.front().first_point();
                if (!extrusion.is_closed) {
                    // Especially for open extrusion, we need to select a starting point that is at the start
                    // or the end of the extrusions to make one continuous line. Also, we prefer a non-overhang
                    // starting point.
                    struct PointInfo
                    {
                        size_t occurrence  = 0;
                        bool   is_overhang = false;
                    };
                    ankerl::unordered_dense::map<Point, PointInfo, PointHash> point_occurrence;
                    for (const ExtrusionPath &path : paths) {
                        ++point_occurrence[path.polyline.first_point()].occurrence;
                        ++point_occurrence[path.polyline.last_point()].occurrence;
                        if (path.role().is_bridge()) {
                            point_occurrence[path.polyline.first_point()].is_overhang = true;
                            point_occurrence[path.polyline.last_point()].is_overhang  = true;
                        }
                    }

                    // Prefer non-overhang point as a starting point.
                    for (const std::pair<Point, PointInfo> &pt : point_occurrence)
                        if (pt.second.occurrence == 1) {
                            start_point = pt.first;
                            if (!pt.second.is_overhang) {
                                start_point = pt.first;
                                break;
                            }
                        }
                }

                chain_and_reorder_extrusion_paths(paths, &start_point);
            }
        } else {
            extrusion_paths_append(paths, extrusion, role_normal, is_external ? params.ext_perimeter_flow : params.perimeter_flow);
        }

        // Append paths to collection.
        if (!paths.empty()) {
            if (extrusion.is_closed) {
                ExtrusionLoop extrusion_loop(std::move(paths));
                // Restore the orientation of the extrusion loop.
                if (pg_extrusion.is_contour() == extrusion_loop.is_clockwise())
                    extrusion_loop.reverse_loop();

                for (auto it = std::next(extrusion_loop.paths.begin()); it != extrusion_loop.paths.end(); ++it) {
                    assert(it->polyline.points.size() >= 2);
                    assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
                }
                assert(extrusion_loop.paths.front().first_point() == extrusion_loop.paths.back().last_point());

                extrusion_coll.append(std::move(extrusion_loop));
            } else {
                // Because we are processing one ExtrusionLine all ExtrusionPaths should form one connected path.
                // But there is possibility that due to numerical issue there is poss
                assert([&paths = std::as_const(paths)]() -> bool {
                    for (auto it = std::next(paths.begin()); it != paths.end(); ++it)
                        if (std::prev(it)->polyline.last_point() != it->polyline.first_point())
                            return false;
                    return true;
                }());
                ExtrusionMultiPath multi_path;
                multi_path.paths.emplace_back(std::move(paths.front()));

                for (auto it_path = std::next(paths.begin()); it_path != paths.end(); ++it_path) {
                    if (multi_path.paths.back().last_point() != it_path->first_point()) {
                        extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
                        multi_path = ExtrusionMultiPath();
                    }
                    multi_path.paths.emplace_back(std::move(*it_path));
                }

                extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
            }
        }
    }

    return extrusion_coll;
}

#ifdef ARACHNE_DEBUG
static void export_perimeters_to_svg(const std::string &path, const Polygons &contours, const Arachne::Perimeters &perimeters, const ExPolygons &infill_area)
{
    coordf_t    stroke_width = scale_(0.03);
    BoundingBox bbox         = get_extents(contours);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw(infill_area, "cyan");

    for (const Arachne::Perimeter &perimeter : perimeters)
        for (const Arachne::ExtrusionLine &extrusion_line : perimeter) {
            ThickPolyline thick_polyline = to_thick_polyline(extrusion_line);
            svg.draw({thick_polyline}, "green", "blue", stroke_width);
        }

    for (const Line &line : to_lines(contours))
        svg.draw(line, "red", stroke_width);
}
#endif

// find out if paths touch - at least one point of one path is within limit distance of second path
bool paths_touch(const ExtrusionPath &path_one, const ExtrusionPath &path_two, double limit_distance)
{
    AABBTreeLines::LinesDistancer<Line> lines_two{path_two.as_polyline().lines()};
    for (size_t pt_idx = 0; pt_idx < path_one.polyline.size(); pt_idx++) {
        if (lines_two.distance_from_lines<false>(path_one.polyline.points[pt_idx]) < limit_distance) { return true; }
    }
    AABBTreeLines::LinesDistancer<Line> lines_one{path_one.as_polyline().lines()};
    for (size_t pt_idx = 0; pt_idx < path_two.polyline.size(); pt_idx++) {
        if (lines_one.distance_from_lines<false>(path_two.polyline.points[pt_idx]) < limit_distance) { return true; }
    }
    return false;
}

Polylines reconnect_polylines(const Polylines &polylines, double limit_distance)
{
    if (polylines.empty())
        return polylines;

    std::unordered_map<size_t, Polyline> connected;
    connected.reserve(polylines.size());
    for (size_t i = 0; i < polylines.size(); i++) {
        if (!polylines[i].empty()) {
            connected.emplace(i, polylines[i]);
        }
    }

    for (size_t a = 0; a < polylines.size(); a++) {
        if (connected.find(a) == connected.end()) {
            continue;
        }
        Polyline &base = connected.at(a);
        for (size_t b = a + 1; b < polylines.size(); b++) {
            if (connected.find(b) == connected.end()) {
                continue;
            }
            Polyline &next = connected.at(b);
            if ((base.last_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.append(std::move(next));
                connected.erase(b);
            } else if ((base.last_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.points.insert(base.points.end(), next.points.rbegin(), next.points.rend());
                connected.erase(b);
            } else if ((base.first_point() - next.last_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                next.append(std::move(base));
                base = std::move(next);
                base.reverse();
                connected.erase(b);
            } else if ((base.first_point() - next.first_point()).cast<double>().squaredNorm() < limit_distance * limit_distance) {
                base.reverse();
                base.append(std::move(next));
                base.reverse();
                connected.erase(b);
            }
        }
    }

    Polylines result;
    for (auto &ext : connected) {
        result.push_back(std::move(ext.second));
    }

    return result;
}

ExtrusionPaths sort_extra_perimeters(const ExtrusionPaths& extra_perims, int index_of_first_unanchored, double extrusion_spacing)
{
    if (extra_perims.empty()) return {};

    std::vector<std::unordered_set<size_t>> dependencies(extra_perims.size());
    for (size_t path_idx = 0; path_idx < extra_perims.size(); path_idx++) {
        for (size_t prev_path_idx = 0; prev_path_idx < path_idx; prev_path_idx++) {
            if (paths_touch(extra_perims[path_idx], extra_perims[prev_path_idx], extrusion_spacing * 1.5f)) {
                       dependencies[path_idx].insert(prev_path_idx);        
            }
        }
    }

    std::vector<bool> processed(extra_perims.size(), false);
    for (int path_idx = 0; path_idx < index_of_first_unanchored; path_idx++) {
        processed[path_idx] = true;
    }

    for (size_t i = index_of_first_unanchored; i < extra_perims.size(); i++) {
        bool change = false;
        for (size_t path_idx = index_of_first_unanchored; path_idx < extra_perims.size(); path_idx++) {
            if (processed[path_idx])
                       continue;
            auto processed_dep = std::find_if(dependencies[path_idx].begin(), dependencies[path_idx].end(),
                                              [&](size_t dep) { return processed[dep]; });
            if (processed_dep != dependencies[path_idx].end()) {
                for (auto it = dependencies[path_idx].begin(); it != dependencies[path_idx].end();) {
                    if (!processed[*it]) {
                        dependencies[*it].insert(path_idx);
                        dependencies[path_idx].erase(it++);
                    } else {
                        ++it;
                    }
                }
                processed[path_idx] = true;
                change              = true;
            }
        }
        if (!change) {
            break;
        }
    }

    Point current_point = extra_perims.begin()->first_point();

    ExtrusionPaths sorted_paths{};
    size_t         null_idx = size_t(-1);
    size_t         next_idx = null_idx;
    bool           reverse  = false;
    while (true) {
        if (next_idx == null_idx) { // find next pidx to print
            double dist = std::numeric_limits<double>::max();
            for (size_t path_idx = 0; path_idx < extra_perims.size(); path_idx++) {
                if (!dependencies[path_idx].empty())
                    continue;
                const auto &path   = extra_perims[path_idx];
                double      dist_a = (path.first_point() - current_point).cast<double>().squaredNorm();
                if (dist_a < dist) {
                    dist     = dist_a;
                    next_idx = path_idx;
                    reverse  = false;
                }
                double dist_b = (path.last_point() - current_point).cast<double>().squaredNorm();
                if (dist_b < dist) {
                    dist     = dist_b;
                    next_idx = path_idx;
                    reverse  = true;
                }
            }
            if (next_idx == null_idx) {
                       break;
            }
        } else {
            // we have valid next_idx, add it to the sorted paths, update dependencies, update current point and potentialy set new next_idx
            ExtrusionPath path = extra_perims[next_idx];
            if (reverse) {
                path.reverse();
            }
            sorted_paths.push_back(path);
            assert(dependencies[next_idx].empty());
            dependencies[next_idx].insert(null_idx);
            current_point = sorted_paths.back().last_point();
            for (size_t path_idx = 0; path_idx < extra_perims.size(); path_idx++) {
                dependencies[path_idx].erase(next_idx);
            }
            double dist = std::numeric_limits<double>::max();
            next_idx    = null_idx;

            for (size_t path_idx = next_idx + 1; path_idx < extra_perims.size(); path_idx++) {
                if (!dependencies[path_idx].empty()) {
                    continue;
                }
                const ExtrusionPath &next_path = extra_perims[path_idx];
                double               dist_a    = (next_path.first_point() - current_point).cast<double>().squaredNorm();
                if (dist_a < dist) {
                    dist     = dist_a;
                    next_idx = path_idx;
                    reverse  = false;
                }
                double dist_b = (next_path.last_point() - current_point).cast<double>().squaredNorm();
                if (dist_b < dist) {
                    dist     = dist_b;
                    next_idx = path_idx;
                    reverse  = true;
                }
            }
            if (dist > scaled(5.0)) {
                next_idx = null_idx;
            }
        }
    }

    ExtrusionPaths reconnected;
    reconnected.reserve(sorted_paths.size());
    for (const ExtrusionPath &path : sorted_paths) {
        if (!reconnected.empty() && (reconnected.back().last_point() - path.first_point()).cast<double>().squaredNorm() <
                                        extrusion_spacing * extrusion_spacing * 4.0) {
            reconnected.back().polyline.points.insert(reconnected.back().polyline.points.end(), path.polyline.points.begin(),
                                                      path.polyline.points.end());
        } else {
            reconnected.push_back(path);
        }
    }

    ExtrusionPaths filtered;
    filtered.reserve(reconnected.size());
    for (ExtrusionPath &p : reconnected) {
        if (p.length() > 3 * extrusion_spacing) {
            filtered.push_back(p);
        }
    }

    return filtered;
}

#define EXTRA_PERIMETER_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
// #define EXTRA_PERIM_DEBUG_FILES
// Function will generate extra perimeters clipped over nonbridgeable areas of the provided surface and returns both the new perimeters and
// Polygons filled by those clipped perimeters
std::tuple<std::vector<ExtrusionPaths>, Polygons> generate_extra_perimeters_over_overhangs(ExPolygons               infill_area,
                                                                                           const Polygons          &lower_slices_polygons,
                                                                                           int                      perimeter_count,
                                                                                           const Flow              &overhang_flow,
                                                                                           double                   scaled_resolution,
                                                                                           const PrintObjectConfig &object_config,
                                                                                           const PrintConfig       &print_config)
{
    coord_t anchors_size = std::min(coord_t(scale_(EXTERNAL_INFILL_MARGIN)), overhang_flow.scaled_spacing() * (perimeter_count + 1));

    BoundingBox infill_area_bb = get_extents(infill_area).inflated(SCALED_EPSILON);
    Polygons optimized_lower_slices = ClipperUtils::clip_clipper_polygons_with_subject_bbox(lower_slices_polygons, infill_area_bb);
    Polygons overhangs  = diff(infill_area, optimized_lower_slices);

    if (overhangs.empty()) { return {}; }

    AABBTreeLines::LinesDistancer<Line> lower_layer_aabb_tree{to_lines(optimized_lower_slices)};
    Polygons                            anchors             = intersection(infill_area, optimized_lower_slices);
    Polygons                            inset_anchors       = diff(anchors,
                                                                   expand(overhangs, anchors_size + 0.1 * overhang_flow.scaled_width(), EXTRA_PERIMETER_OFFSET_PARAMETERS));
    Polygons                            inset_overhang_area = diff(infill_area, inset_anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
    {
        BoundingBox bbox = get_extents(inset_overhang_area);
        bbox.offset(scale_(1.));
        ::Slic3r::SVG svg(debug_out_path("inset_overhang_area").c_str(), bbox);
        for (const Line &line : to_lines(inset_anchors)) svg.draw(line, "purple", scale_(0.25));
        for (const Line &line : to_lines(inset_overhang_area)) svg.draw(line, "red", scale_(0.15));
        svg.Close();
    }
#endif

    Polygons inset_overhang_area_left_unfilled;

    std::vector<ExtrusionPaths> extra_perims; // overhang region -> extrusion paths
    for (const ExPolygon &overhang : union_ex(to_expolygons(inset_overhang_area))) {
        Polygons overhang_to_cover = to_polygons(overhang);
        Polygons expanded_overhang_to_cover = expand(overhang_to_cover, 1.1 * overhang_flow.scaled_spacing());
        Polygons shrinked_overhang_to_cover = shrink(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing());

        Polygons real_overhang = intersection(overhang_to_cover, overhangs);
        if (real_overhang.empty()) {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), overhang_to_cover.begin(),
                                                     overhang_to_cover.end());
            continue;
        }
        ExtrusionPaths &overhang_region = extra_perims.emplace_back();

        Polygons anchoring         = intersection(expanded_overhang_to_cover, inset_anchors);
        Polygons perimeter_polygon = offset(union_(expand(overhang_to_cover, 0.1 * overhang_flow.scaled_spacing()), anchoring),
                                            -overhang_flow.scaled_spacing() * 0.6);

        Polygon anchoring_convex_hull = Geometry::convex_hull(anchoring);
        double  unbridgeable_area     = area(diff(real_overhang, {anchoring_convex_hull}));

        auto [dir, unsupp_dist] = detect_bridging_direction(real_overhang, anchors);

#ifdef EXTRA_PERIM_DEBUG_FILES
        {
            BoundingBox bbox = get_extents(anchoring_convex_hull);
            bbox.offset(scale_(1.));
            ::Slic3r::SVG svg(debug_out_path("bridge_check").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "purple", scale_(0.25));
            for (const Line &line : to_lines(real_overhang)) svg.draw(line, "red", scale_(0.20));
            for (const Line &line : to_lines(anchoring_convex_hull)) svg.draw(line, "green", scale_(0.15));
            for (const Line &line : to_lines(anchoring)) svg.draw(line, "yellow", scale_(0.10));
            for (const Line &line : to_lines(diff_ex(perimeter_polygon, {anchoring_convex_hull}))) svg.draw(line, "black", scale_(0.10));
            for (const Line &line : to_lines(diff_pl(to_polylines(diff(real_overhang, anchors)), expand(anchors, float(SCALED_EPSILON)))))
                svg.draw(line, "blue", scale_(0.30));
            svg.Close();
        }
#endif

        if (unbridgeable_area < 0.2 * area(real_overhang) && unsupp_dist < total_length(real_overhang) * 0.2) {
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(),overhang_to_cover.begin(),overhang_to_cover.end());
            perimeter_polygon.clear();
        } else {
            //  fill the overhang with perimeters
            int continuation_loops = 2;
            while (continuation_loops >= 0) {
                auto prev = perimeter_polygon;
                // prepare next perimeter lines
                Polylines perimeter = intersection_pl(to_polylines(perimeter_polygon), shrinked_overhang_to_cover);

                // do not add the perimeter to result yet, first check if perimeter_polygon is not empty after shrinking - this would mean
                //  that the polygon was possibly too small for full perimeter loop and in that case try gap fill first
                perimeter_polygon = union_(perimeter_polygon, anchoring);
                perimeter_polygon = intersection(offset(perimeter_polygon, -overhang_flow.scaled_spacing()), expanded_overhang_to_cover);

                if (perimeter_polygon.empty()) { // fill possible gaps of single extrusion width
                    Polygons shrinked = intersection(offset(prev, -0.3 * overhang_flow.scaled_spacing()), expanded_overhang_to_cover);
                    if (!shrinked.empty())
                        extrusion_paths_append(overhang_region, reconnect_polylines(perimeter, overhang_flow.scaled_spacing()),
                                               ExtrusionAttributes{ ExtrusionRole::OverhangPerimeter, overhang_flow });

                    Polylines  fills;
                    ExPolygons gap = shrinked.empty() ? offset_ex(prev, overhang_flow.scaled_spacing() * 0.5) : to_expolygons(shrinked);

                    for (const ExPolygon &ep : gap) {
                        ep.medial_axis(0.75 * overhang_flow.scaled_width(), 3.0 * overhang_flow.scaled_spacing(), &fills);
                    }
                    if (!fills.empty()) {
                        fills = intersection_pl(fills, shrinked_overhang_to_cover);
                        extrusion_paths_append(overhang_region, reconnect_polylines(fills, overhang_flow.scaled_spacing()),
                                               ExtrusionAttributes{ ExtrusionRole::OverhangPerimeter, overhang_flow });
                    }
                    break;
                } else {
                    extrusion_paths_append(overhang_region, reconnect_polylines(perimeter, overhang_flow.scaled_spacing()),
                                           ExtrusionAttributes{ExtrusionRole::OverhangPerimeter, overhang_flow });
                }

                if (intersection(perimeter_polygon, real_overhang).empty()) { continuation_loops--; }

                if (prev == perimeter_polygon) {
#ifdef EXTRA_PERIM_DEBUG_FILES
                    BoundingBox bbox = get_extents(perimeter_polygon);
                    bbox.offset(scale_(5.));
                    ::Slic3r::SVG svg(debug_out_path("perimeter_polygon").c_str(), bbox);
                    for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "blue", scale_(0.25));
                    for (const Line &line : to_lines(overhang_to_cover)) svg.draw(line, "red", scale_(0.20));
                    for (const Line &line : to_lines(real_overhang)) svg.draw(line, "green", scale_(0.15));
                    for (const Line &line : to_lines(anchoring)) svg.draw(line, "yellow", scale_(0.10));
                    svg.Close();
#endif
                    break;
                }
            }

            perimeter_polygon = expand(perimeter_polygon, 0.5 * overhang_flow.scaled_spacing());
            perimeter_polygon = union_(perimeter_polygon, anchoring);
            inset_overhang_area_left_unfilled.insert(inset_overhang_area_left_unfilled.end(), perimeter_polygon.begin(),perimeter_polygon.end());

#ifdef EXTRA_PERIM_DEBUG_FILES
            BoundingBox bbox = get_extents(inset_overhang_area);
            bbox.offset(scale_(2.));
            ::Slic3r::SVG svg(debug_out_path("pre_final").c_str(), bbox);
            for (const Line &line : to_lines(perimeter_polygon)) svg.draw(line, "blue", scale_(0.05));
            for (const Line &line : to_lines(anchoring)) svg.draw(line, "green", scale_(0.05));
            for (const Line &line : to_lines(overhang_to_cover)) svg.draw(line, "yellow", scale_(0.05));
            for (const Line &line : to_lines(inset_overhang_area_left_unfilled)) svg.draw(line, "red", scale_(0.05));
            svg.Close();
#endif
            overhang_region.erase(std::remove_if(overhang_region.begin(), overhang_region.end(),
                                                 [](const ExtrusionPath &p) { return p.empty(); }),
                                  overhang_region.end());

            if (!overhang_region.empty()) {
                // there is a special case, where the first (or last) generated overhang perimeter eats all anchor space.
                // When this happens, the first overhang perimeter is also a closed loop, and needs special check
                // instead of the following simple is_anchored lambda, which checks only the first and last point (not very useful on closed
                // polyline)
                bool first_overhang_is_closed_and_anchored =
                    (overhang_region.front().first_point() == overhang_region.front().last_point() &&
                     !intersection_pl(overhang_region.front().polyline, optimized_lower_slices).empty());
                     
                auto is_anchored = [&lower_layer_aabb_tree](const ExtrusionPath &path) {
                    return lower_layer_aabb_tree.distance_from_lines<true>(path.first_point()) <= 0 ||
                           lower_layer_aabb_tree.distance_from_lines<true>(path.last_point()) <= 0;
                };
                if (!first_overhang_is_closed_and_anchored) {
                    std::reverse(overhang_region.begin(), overhang_region.end());
                } else {
                    size_t min_dist_idx = 0;
                    double min_dist = std::numeric_limits<double>::max();
                    for (size_t i = 0; i < overhang_region.front().polyline.size(); i++) {
                        Point p = overhang_region.front().polyline[i];
                        if (double d = lower_layer_aabb_tree.distance_from_lines<true>(p) < min_dist) {
                            min_dist = d;
                            min_dist_idx = i;
                        }
                    }
                    std::rotate(overhang_region.front().polyline.begin(), overhang_region.front().polyline.begin() + min_dist_idx,
                                overhang_region.front().polyline.end());
                }
                auto first_unanchored          = std::stable_partition(overhang_region.begin(), overhang_region.end(), is_anchored);
                int  index_of_first_unanchored = first_unanchored - overhang_region.begin();
                overhang_region = sort_extra_perimeters(overhang_region, index_of_first_unanchored, overhang_flow.scaled_spacing());
            }
        }
    }

#ifdef EXTRA_PERIM_DEBUG_FILES
    BoundingBox bbox = get_extents(inset_overhang_area);
    bbox.offset(scale_(2.));
    ::Slic3r::SVG svg(debug_out_path(("final" + std::to_string(rand())).c_str()).c_str(), bbox);
    for (const Line &line : to_lines(inset_overhang_area_left_unfilled)) svg.draw(line, "blue", scale_(0.05));
    for (const Line &line : to_lines(inset_overhang_area)) svg.draw(line, "green", scale_(0.05));
    for (const Line &line : to_lines(diff(inset_overhang_area, inset_overhang_area_left_unfilled))) svg.draw(line, "yellow", scale_(0.05));
    svg.Close();
#endif

    inset_overhang_area_left_unfilled = union_(inset_overhang_area_left_unfilled);

    return {extra_perims, diff(inset_overhang_area, inset_overhang_area_left_unfilled)};
}

// Thanks, Cura developers, for implementing an algorithm for generating perimeters with variable width (Arachne) that is based on the paper
// "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling"
void PerimeterGenerator::process_arachne(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  & /* out_gap_fill */,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons)
{
    // other perimeters
    coord_t perimeter_width        = params.perimeter_flow.scaled_width();
    coord_t perimeter_spacing      = params.perimeter_flow.scaled_spacing();
    // external perimeters
    coord_t ext_perimeter_width    = params.ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing  = params.ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2 = scaled<coord_t>(0.5f * (params.ext_perimeter_flow.spacing() + params.perimeter_flow.spacing()));
    // solid infill
    coord_t solid_infill_spacing  = params.solid_infill_flow.scaled_spacing();

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty()) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used
        // in the current layer
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder-1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    // detect how many perimeters must be generated for this island
    int loop_number = params.config.perimeters + surface.extra_perimeters - 1; // 0-indexed loops
    if (loop_number > 0 && ((params.config.top_one_perimeter_type == TopOnePerimeterType::TopmostOnly && upper_slices == nullptr) || (params.config.only_one_perimeter_first_layer && params.layer_id == 0)))
        loop_number = 0;

    // Calculate how many inner loops remain when TopSurfaces is selected.
    const int inner_loop_number = (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces && upper_slices != nullptr) ? loop_number - 1 : -1;

    // Set one perimeter when TopSurfaces is selected.
    if (params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces)
        loop_number = 0;

    ExPolygons last   = offset_ex(surface.expolygon.simplify_p(params.scaled_resolution), - float(ext_perimeter_width / 2. - ext_perimeter_spacing / 2.));
    Polygons   last_p = to_polygons(last);
    Arachne::WallToolPaths wall_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(loop_number + 1), 0, params.layer_height, params.object_config, params.print_config);
    Arachne::Perimeters    perimeters     = wall_tool_paths.getToolPaths();
    ExPolygons             infill_contour = union_ex(wall_tool_paths.getInnerContour());

    // Check if there are some remaining perimeters to generate (the number of perimeters
    // is greater than one together with enabled the single perimeter on top surface feature).
    if (inner_loop_number >= 0) {
        assert(upper_slices != nullptr);

        // Infill contour bounding box.
        BoundingBox infill_contour_bbox = get_extents(infill_contour);
        infill_contour_bbox.offset(SCALED_EPSILON);

        // Get top ExPolygons from current infill contour.
        const Polygons upper_slices_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*upper_slices, infill_contour_bbox);
        ExPolygons     top_expolygons       = diff_ex(infill_contour, upper_slices_clipped);

        if (!top_expolygons.empty()) {
            if (lower_slices != nullptr) {
                const float      bridge_offset          = float(std::max<coord_t>(ext_perimeter_spacing, perimeter_width));
                const Polygons   lower_slices_clipped   = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*lower_slices, infill_contour_bbox);
                const ExPolygons current_slices_bridges = offset_ex(diff_ex(top_expolygons, lower_slices_clipped), bridge_offset);

                // Remove bridges from top surface polygons.
                top_expolygons = diff_ex(top_expolygons, current_slices_bridges);
            }

            // Filter out areas that are too thin and expand top surface polygons a bit to hide the wall line.
            const float top_surface_min_width = std::max<float>(float(ext_perimeter_spacing) / 4.f + scaled<float>(0.00001), float(perimeter_width) / 4.f);
            top_expolygons = offset2_ex(top_expolygons, -top_surface_min_width, top_surface_min_width + float(perimeter_width));

            // Get the not-top ExPolygons (including bridges) from current slices and expanded real top ExPolygons (without bridges).
            const ExPolygons not_top_expolygons = diff_ex(infill_contour, top_expolygons);

            // Get final top ExPolygons.
            top_expolygons = intersection_ex(top_expolygons, infill_contour);

            const Polygons not_top_polygons = to_polygons(not_top_expolygons);
            Arachne::WallToolPaths inner_wall_tool_paths(not_top_polygons, perimeter_spacing, perimeter_spacing, coord_t(inner_loop_number + 1), 0, params.layer_height, params.object_config, params.print_config);
            Arachne::Perimeters inner_perimeters = inner_wall_tool_paths.getToolPaths();

            // Recalculate indexes of inner perimeters before merging them.
            if (!perimeters.empty()) {
                for (Arachne::VariableWidthLines &inner_perimeter : inner_perimeters) {
                    if (inner_perimeter.empty())
                        continue;

                    for (Arachne::ExtrusionLine &el : inner_perimeter)
                        ++el.inset_idx;
                }
            }

            perimeters.insert(perimeters.end(), inner_perimeters.begin(), inner_perimeters.end());
            infill_contour = union_ex(top_expolygons, inner_wall_tool_paths.getInnerContour());
        } else {
            // There is no top surface ExPolygon, so we call Arachne again with parameters
            // like when the single perimeter feature is disabled.
            Arachne::WallToolPaths no_single_perimeter_tool_paths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(inner_loop_number + 2), 0, params.layer_height, params.object_config, params.print_config);
            perimeters     = no_single_perimeter_tool_paths.getToolPaths();
            infill_contour = union_ex(no_single_perimeter_tool_paths.getInnerContour());
        }
    }

    loop_number = int(perimeters.size()) - 1;

#ifdef ARACHNE_DEBUG
    {
        static int iRun = 0;
        export_perimeters_to_svg(debug_out_path("arachne-perimeters-%d-%d.svg", params.layer_id, iRun++), to_polygons(last), perimeters, union_ex(wallToolPaths.getInnerContour()));
    }
#endif

    // All closed ExtrusionLine should have the same the first and the last point.
    // But in rare cases, Arachne produce ExtrusionLine marked as closed but without
    // equal the first and the last point.
    assert([&perimeters = std::as_const(perimeters)]() -> bool {
        for (const Arachne::Perimeter &perimeter : perimeters)
            for (const Arachne::ExtrusionLine &el : perimeter)
                if (el.is_closed && el.junctions.front().p != el.junctions.back().p)
                    return false;
        return true;
    }());

    Arachne::PerimeterOrder::PerimeterExtrusions ordered_extrusions = Arachne::PerimeterOrder::ordered_perimeter_extrusions(perimeters, params.config.external_perimeters_first);

    if (params.layer_id > 0 && params.config.fuzzy_skin != FuzzySkinType::None) {
        std::vector<Arachne::PerimeterOrder::PerimeterExtrusion *> closed_loop_extrusions;
        for (Arachne::PerimeterOrder::PerimeterExtrusion &extrusion : ordered_extrusions)
            if (extrusion.extrusion.inset_idx == 0) {
                if (extrusion.extrusion.is_closed && params.config.fuzzy_skin == FuzzySkinType::External) {
                    closed_loop_extrusions.emplace_back(&extrusion);
                } else {
                    extrusion.fuzzify = true;
                }
            }

        if (params.config.fuzzy_skin == FuzzySkinType::External) {
            ClipperLib_Z::Paths loops_paths;
            loops_paths.reserve(closed_loop_extrusions.size());
            for (const auto &cl_extrusion : closed_loop_extrusions) {
                assert(cl_extrusion->extrusion.junctions.front() == cl_extrusion->extrusion.junctions.back());
                size_t             loop_idx = &cl_extrusion - &closed_loop_extrusions.front();
                ClipperLib_Z::Path loop_path;
                loop_path.reserve(cl_extrusion->extrusion.junctions.size() - 1);
                for (auto junction_it = cl_extrusion->extrusion.junctions.begin(); junction_it != std::prev(cl_extrusion->extrusion.junctions.end()); ++junction_it)
                    loop_path.emplace_back(junction_it->p.x(), junction_it->p.y(), loop_idx);
                loops_paths.emplace_back(loop_path);
            }

            ClipperLib_Z::Clipper clipper;
            clipper.AddPaths(loops_paths, ClipperLib_Z::ptSubject, true);
            ClipperLib_Z::PolyTree loops_polytree;
            clipper.Execute(ClipperLib_Z::ctUnion, loops_polytree, ClipperLib_Z::pftEvenOdd, ClipperLib_Z::pftEvenOdd);

            for (const ClipperLib_Z::PolyNode *child_node : loops_polytree.Childs) {
                // The whole contour must have the same index.
                coord_t polygon_idx  = child_node->Contour.front().z();
                bool    has_same_idx = std::all_of(child_node->Contour.begin(), child_node->Contour.end(),
                                                   [&polygon_idx](const ClipperLib_Z::IntPoint &point) -> bool { return polygon_idx == point.z(); });
                if (has_same_idx)
                    closed_loop_extrusions[polygon_idx]->fuzzify = true;
            }
        }
    }

    if (ExtrusionEntityCollection extrusion_coll = traverse_extrusions(params, lower_slices_polygons_cache, ordered_extrusions); !extrusion_coll.empty())
        out_loops.append(extrusion_coll);

    const coord_t spacing = (perimeters.size() == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
    if (offset_ex(infill_contour, -float(spacing / 2.)).empty())
        infill_contour.clear(); // Infill region is too small, so let's filter it out.

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset =
        (loop_number < 0) ? 0 :
        (loop_number == 0) ?
                            // one loop
            ext_perimeter_spacing:
            // two or more loops?
            perimeter_spacing;

    inset = coord_t(scale_(params.config.get_abs_value("infill_overlap", unscale<double>(inset))));
    Polygons pp;
    for (ExPolygon &ex : infill_contour)
        ex.simplify_p(params.scaled_resolution, &pp);
    // collapse too narrow infill areas
    const auto    min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    // append infill areas to fill_surfaces
    ExPolygons infill_areas =
        offset2_ex(
            union_ex(pp),
            float(- min_perimeter_infill_spacing / 2.),
            float(inset + min_perimeter_infill_spacing / 2.));

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers) {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters, filled_area] = generate_extra_perimeters_over_overhangs(infill_areas,
                                                                                        lower_slices_polygons_cache,
                                                                                        loop_number + 1,
                                                                                        params.overhang_flow, params.scaled_resolution,
                                                                                        params.object_config, params.print_config);
        if (!extra_perimeters.empty()) {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection&>(*out_loops.entities.back());
            ExtrusionEntitiesPtr       old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters) 
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }
    
    append(out_fill_expolygons, std::move(infill_areas));
}

void PerimeterGenerator::process_classic(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons)
{
    // other perimeters
    coord_t perimeter_width         = params.perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = params.perimeter_flow.scaled_spacing();
    // external perimeters
    coord_t ext_perimeter_width     = params.ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = params.ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = scaled<coord_t>(0.5f * (params.ext_perimeter_flow.spacing() + params.perimeter_flow.spacing()));
    // solid infill
    coord_t solid_infill_spacing    = params.solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = coord_t(perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE));
    coord_t ext_min_spacing     = coord_t(ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE));
    bool    has_gap_fill 		= params.config.gap_fill_enabled.value && params.config.gap_fill_speed.value > 0;

    // prepare grown lower layer slices for overhang detection
    if (params.config.overhangs && lower_slices != nullptr && lower_slices_polygons_cache.empty()) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = params.print_config.nozzle_diameter.get_at(params.config.perimeter_extruder-1);
        lower_slices_polygons_cache = offset(*lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    // detect how many perimeters must be generated for this island
    int        loop_number = params.config.perimeters + surface.extra_perimeters - 1;  // 0-indexed loops

    // Set the topmost layer to be one perimeter.
    if (loop_number > 0 && ((params.config.top_one_perimeter_type != TopOnePerimeterType::None && upper_slices == nullptr) || (params.config.only_one_perimeter_first_layer && params.layer_id == 0)))
        loop_number = 0;

    ExPolygons last = union_ex(surface.expolygon.simplify_p(params.scaled_resolution));
    ExPolygons gaps;
    ExPolygons top_fills;
    ExPolygons fill_clip;
    if (loop_number >= 0) {
        // In case no perimeters are to be generated, loop_number will equal to -1.
        std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
        std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
        ThickPolylines thin_walls;
        // we loop one time more than needed in order to find gaps after the last perimeter was applied
        for (int i = 0;; ++ i) {  // outer loop is 0
            // Calculate next onion shell of perimeters.
            ExPolygons offsets;
            if (i == 0) {
                // the minimum thickness of a single loop is:
                // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                offsets = params.config.thin_walls ? 
                    offset2_ex(
                        last,
                        - float(ext_perimeter_width / 2. + ext_min_spacing / 2. - 1),
                        + float(ext_min_spacing / 2. - 1)) :
                    offset_ex(last, - float(ext_perimeter_width / 2.));
                // look for thin walls
                if (params.config.thin_walls) {
                    // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                    // (actually, something larger than that still may exist due to mitering or other causes)
                    coord_t min_width = coord_t(scale_(params.ext_perimeter_flow.nozzle_diameter() / 3));
                    ExPolygons expp = opening_ex(
                        // medial axis requires non-overlapping geometry
                        diff_ex(last, offset(offsets, float(ext_perimeter_width / 2.) + ClipperSafetyOffset)),
                        float(min_width / 2.));
                    // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                    for (ExPolygon &ex : expp)
                        ex.medial_axis(min_width, ext_perimeter_width + ext_perimeter_spacing2, &thin_walls);
                }
                if (params.spiral_vase && offsets.size() > 1) {
                	// Remove all but the largest area polygon.
                	keep_largest_contour_only(offsets);
                }
            } else {
                //FIXME Is this offset correct if the line width of the inner perimeters differs
                // from the line width of the infill?
                coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                offsets = params.config.thin_walls ?
                    // This path will ensure, that the perimeters do not overfill, as in 
                    // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                    // excessively, creating gaps, which then need to be filled in by the not very 
                    // reliable gap fill algorithm.
                    // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                    // the original.
                    offset2_ex(last,
                            - float(distance + min_spacing / 2. - 1.),
                            float(min_spacing / 2. - 1.)) :
                    // If "detect thin walls" is not enabled, this paths will be entered, which 
                    // leads to overflows, as in prusa3d/Slic3r GH #32
                    offset_ex(last, - float(distance));
                // look for gaps
                if (has_gap_fill)
                    // not using safety offset here would "detect" very narrow gaps
                    // (but still long enough to escape the area threshold) that gap fill
                    // won't be able to fill but we'd still remove from infill area
                    append(gaps, diff_ex(
                        offset(last,    - float(0.5 * distance)),
                        offset(offsets,   float(0.5 * distance + 10))));  // safety offset
            }
            if (offsets.empty()) {
                // Store the number of loops actually generated.
                loop_number = i - 1;
                // No region left to be filled in.
                last.clear();
                break;
            } else if (i > loop_number) {
                // If i > loop_number, we were looking just for gaps.
                break;
            }
            {
                const bool fuzzify_contours = params.config.fuzzy_skin != FuzzySkinType::None && i == 0 && params.layer_id > 0;
                const bool fuzzify_holes    = fuzzify_contours && params.config.fuzzy_skin == FuzzySkinType::All;
                for (const ExPolygon &expolygon : offsets) {
	                // Outer contour may overlap with an inner contour,
	                // inner contour may overlap with another inner contour,
	                // outer contour may overlap with itself.
	                //FIXME evaluate the overlaps, annotate each point with an overlap depth,
                    // compensate for the depth of intersection.
                    contours[i].emplace_back(expolygon.contour, i, true, fuzzify_contours);

                    if (! expolygon.holes.empty()) {
                        holes[i].reserve(holes[i].size() + expolygon.holes.size());
                        for (const Polygon &hole : expolygon.holes)
                            holes[i].emplace_back(hole, i, false, fuzzify_holes);
                    }
                }
            }
            last = std::move(offsets);

            // Store surface for top infill if top_one_perimeter_type is set to TopSurfaces.
            if (i == 0 && i != loop_number && params.config.top_one_perimeter_type == TopOnePerimeterType::TopSurfaces && upper_slices != nullptr) {
                // Split the polygons with top/not_top.

                // Get the offset from solid surface anchor.
                const coordf_t total_perimeter_spacing      = coordf_t(perimeter_spacing * (params.config.perimeters.value - 1));
                const coordf_t top_surface_offset_threshold = params.config.perimeters.value <= 1 ? 0. : 0.9 * total_perimeter_spacing;
                coordf_t       top_surface_offset           = params.config.perimeters.value == 0 ? 0. : 1.5 * coordf_t(ext_perimeter_width + total_perimeter_spacing);

                // If possible, try to not push the extra perimeters inside the sparse infill.
                if (top_surface_offset > top_surface_offset_threshold) {
                    top_surface_offset -= top_surface_offset_threshold;
                } else
                    top_surface_offset = 0.;

                // Don't take into account too thin areas.
                const float top_surface_min_width = std::max<float>(float(ext_perimeter_spacing) / 2.f + scaled<float>(0.00001), float(perimeter_width));

                // Current slices bounding box.
                BoundingBox current_perimeters_bbox = get_extents(last);
                current_perimeters_bbox.offset(SCALED_EPSILON);

                ExPolygons current_slices_without_bridges;
                if (lower_slices != nullptr) {
                    const float      bridge_offset          = 1.5f * float(std::max<coord_t>(ext_perimeter_spacing, perimeter_width));
                    const Polygons   lower_slices_clipped   = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*lower_slices, current_perimeters_bbox);
                    const ExPolygons current_slices_bridges = offset_ex(diff_ex(last, lower_slices_clipped, ApplySafetyOffset::Yes), bridge_offset);
                    current_slices_without_bridges = diff_ex(last, current_slices_bridges, ApplySafetyOffset::Yes);
                } else {
                    current_slices_without_bridges = last;
                }

                // Get top ExPolygons (including external perimeters) from current slices without bridges.
                const Polygons   upper_slices_clipped = expand(ClipperUtils::clip_clipper_polygons_with_subject_bbox(*upper_slices, current_perimeters_bbox), top_surface_min_width);
                const ExPolygons top_polygons         = diff_ex(current_slices_without_bridges, upper_slices_clipped, ApplySafetyOffset::Yes);

                if (!top_polygons.empty()) {
                    // Set the clip to a virtual second perimeter.
                    fill_clip = offset_ex(last, -float(ext_perimeter_spacing));

                    // Get the not-top ExPolygons (including bridges) from current slices and expanded real top ExPolygons (without bridges).
                    const ExPolygons not_top_polygons = diff_ex(last, offset_ex(top_polygons, float(top_surface_offset) + top_surface_min_width - (float(ext_perimeter_spacing) / 2.f)), ApplySafetyOffset::Yes);

                    // Get difference between top ExPolygons without bridges and the area defined by the virtual second perimeter.
                    const ExPolygons top_gap          = diff_ex(top_polygons, fill_clip);

                    // Get top infill surface ExPolygons (without bridges) using the difference between the area defined by the virtual second perimeter and non-top ExPolygons.
                    top_fills = diff_ex(fill_clip, not_top_polygons, ApplySafetyOffset::Yes);

                    // Set the clip to the external perimeter but go back inside by infill_extrusion_width/2 to ensure the extrusion won't go outside even with a 100% overlap.
                    fill_clip = offset_ex(last, float((coordf_t(ext_perimeter_spacing) / 2.) - params.config.infill_extrusion_width.get_abs_value(params.solid_infill_flow.nozzle_diameter()) / 2.));
                    last      = intersection_ex(not_top_polygons, last);

                    if (has_gap_fill)
                        last = union_ex(last, top_gap);
                }
            }

            if (i == loop_number && (! has_gap_fill || params.config.fill_density.value == 0)) {
            	// The last run of this loop is executed to collect gaps for gap fill.
            	// As the gap fill is either disabled or not 
            	break;
            }
        }

        // nest loops: holes first
        for (int d = 0; d <= loop_number; ++ d) {
            PerimeterGeneratorLoops &holes_d = holes[d];
            // loop through all holes having depth == d
            for (int i = 0; i < (int)holes_d.size(); ++ i) {
                const PerimeterGeneratorLoop &loop = holes_d[i];
                // find the hole loop that contains this one, if any
                for (int t = d + 1; t <= loop_number; ++ t) {
                    for (int j = 0; j < (int)holes[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            holes_d.erase(holes_d.begin() + i);
                            -- i;
                            goto NEXT_LOOP;
                        }
                    }
                }
                // if no hole contains this hole, find the contour loop that contains it
                for (int t = loop_number; t >= 0; -- t) {
                    for (int j = 0; j < (int)contours[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            holes_d.erase(holes_d.begin() + i);
                            -- i;
                            goto NEXT_LOOP;
                        }
                    }
                }
                NEXT_LOOP: ;
            }
        }
        // nest contour loops
        for (int d = loop_number; d >= 1; -- d) {
            PerimeterGeneratorLoops &contours_d = contours[d];
            // loop through all contours having depth == d
            for (int i = 0; i < (int)contours_d.size(); ++ i) {
                const PerimeterGeneratorLoop &loop = contours_d[i];
                // find the contour loop that contains it
                for (int t = d - 1; t >= 0; -- t) {
                    for (size_t j = 0; j < contours[t].size(); ++ j) {
                        PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                        if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                            candidate_parent.children.push_back(loop);
                            contours_d.erase(contours_d.begin() + i);
                            -- i;
                            goto NEXT_CONTOUR;
                        }
                    }
                }
                NEXT_CONTOUR: ;
            }
        }
        // at this point, all loops should be in contours[0]
        ExtrusionEntityCollection entities = traverse_loops_classic(params, lower_slices_polygons_cache, contours.front(), thin_walls);
        // if brim will be printed, reverse the order of perimeters so that
        // we continue inwards after having finished the brim
        // TODO: add test for perimeter order
        if (params.config.external_perimeters_first || 
            (params.layer_id == 0 && params.object_config.brim_width.value > 0))
            entities.reverse();
        // append perimeters for this slice as a collection
        if (! entities.empty())
            out_loops.append(entities);
    } // for each loop of an island

    // fill gaps
    if (! gaps.empty()) {
        // collapse 
        double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
        double max = 2. * perimeter_spacing;
        ExPolygons gaps_ex = diff_ex(
            //FIXME offset2 would be enough and cheaper.
            opening_ex(gaps, float(min / 2.)),
            offset2_ex(gaps, - float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
        ThickPolylines polylines;
        for (const ExPolygon &ex : gaps_ex)
            ex.medial_axis(min, max, &polylines);
        if (! polylines.empty()) {
			ExtrusionEntityCollection gap_fill;
			variable_width_classic(polylines, ExtrusionRole::GapFill, params.solid_infill_flow, gap_fill.entities);
            /*  Make sure we don't infill narrow parts that are already gap-filled
                (we only consider this surface's gaps to reduce the diff() complexity).
                Growing actual extrusions ensures that gaps not filled by medial axis
                are not subtracted from fill surfaces (they might be too short gaps
                that medial axis skips but infill might join with other infill regions
                and use zigzag).  */
            //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
            // therefore it may cover the area, but no the volume.
            last = diff_ex(last, gap_fill.polygons_covered_by_width(10.f));
			out_gap_fill.append(std::move(gap_fill.entities));
		}
    }

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t inset = 
        (loop_number < 0) ? 0 :
        (loop_number == 0) ?
            // one loop
            ext_perimeter_spacing / 2 :
            // two or more loops?
            perimeter_spacing / 2;

    // Only apply infill overlap if we actually have one perimeter.
    const coord_t infill_perimeter_overlap = (inset > 0) ? coord_t(params.config.get_abs_value("infill_overlap", coordf_t(inset + solid_infill_spacing / 2.))) : 0;
    inset -= infill_perimeter_overlap;

    // simplify infill contours according to resolution
    Polygons pp;
    for (ExPolygon &ex : last)
        ex.simplify_p(params.scaled_resolution, &pp);
    // collapse too narrow infill areas
    coord_t min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
    // append infill areas to fill_surfaces
    ExPolygons infill_areas =
        offset2_ex(
            union_ex(pp),
            float(- inset - min_perimeter_infill_spacing / 2.),
            float(min_perimeter_infill_spacing / 2.));

    // Apply single perimeter feature.
    if (!top_fills.empty()) {
        const ExPolygons top_infill_areas = intersection_ex(fill_clip, offset_ex(top_fills, float(ext_perimeter_spacing) / 2.f));
        infill_areas = union_ex(infill_areas, offset_ex(top_infill_areas, float(infill_perimeter_overlap)));
    }

    if (lower_slices != nullptr && params.config.overhangs && params.config.extra_perimeters_on_overhangs &&
        params.config.perimeters > 0 && params.layer_id > params.object_config.raft_layers) {
        // Generate extra perimeters on overhang areas, and cut them to these parts only, to save print time and material
        auto [extra_perimeters, filled_area] = generate_extra_perimeters_over_overhangs(infill_areas,
                                                                                        lower_slices_polygons_cache,
                                                                                        loop_number + 1,
                                                                                        params.overhang_flow, params.scaled_resolution,
                                                                                        params.object_config, params.print_config);
        if (!extra_perimeters.empty()) {
            ExtrusionEntityCollection &this_islands_perimeters = static_cast<ExtrusionEntityCollection&>(*out_loops.entities.back());
            ExtrusionEntitiesPtr       old_entities;
            old_entities.swap(this_islands_perimeters.entities);
            for (ExtrusionPaths &paths : extra_perimeters) 
                this_islands_perimeters.append(std::move(paths));
            append(this_islands_perimeters.entities, old_entities);
            infill_areas = diff_ex(infill_areas, filled_area);
        }
    }
    
    append(out_fill_expolygons, std::move(infill_areas));
}

}
