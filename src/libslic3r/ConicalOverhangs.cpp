#include "ConicalOverhangs.hpp"

#include "libslic3r/Layer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slic3r {

static const double pi = 3.14159265358979323846;

// Signed turning (exterior) angle at b for the path a -> b -> c, in radians.
// Positive = left turn (convex on a CCW contour), negative = right turn.
double conical_signed_turning_angle(const Vec2d &a, const Vec2d &b, const Vec2d &c)
{
    const Vec2d e_in  = b - a;
    const Vec2d e_out = c - b;
    const double cross = e_in.x() * e_out.y() - e_in.y() * e_out.x();
    const double dot   = e_in.dot(e_out);
    if (e_in.squaredNorm() < 1e-12 || e_out.squaredNorm() < 1e-12)
        return 0.;
    return std::atan2(cross, dot);
}

// One step of discrete curve-shortening (Laplacian) flow of a closed contour:
// move every vertex towards the midpoint of its two neighbours (the curvature
// direction), but no further than `max_travel`. High-curvature convex ridges hit
// the cap and melt inward at that bounded speed; straight (collinear) runs are
// fixed points and stay put. One step per layer - the layer stack integrates the
// flow, so ridges melt away over the stack (a subdivided square rounds into a
// circle). Subtractive at convex corners. Needs a subdivided input; a bare
// 4-vertex square only shrinks, it cannot round.
static Polygon curve_shorten_polygon(const Polygon &poly, double max_travel)
{
    const int n = int(poly.points.size());
    if (n < 4 || max_travel <= 0.)
        return poly;

    const double lambda = 0.5;   // internal flow step; the clamp bounds the distance
    auto wrap = [n](int i) { return (i % n + n) % n; };

    std::vector<Vec2d> p(n);
    for (int i = 0; i < n; ++i)
        p[i] = Vec2d(double(poly.points[i].x()), double(poly.points[i].y()));

    Polygon out;
    out.points.reserve(n);
    for (int i = 0; i < n; ++i) {
        const Vec2d mid = 0.5 * (p[wrap(i - 1)] + p[wrap(i + 1)]);
        Vec2d disp = lambda * (mid - p[i]);
        if (disp.norm() > max_travel)
            disp = disp.normalized() * max_travel;
        const Vec2d q = p[i] + disp;
        out.points.emplace_back(coord_t(std::lround(q.x())), coord_t(std::lround(q.y())));
    }
    return out;
}

// Melt the ridges of every contour and hole, then re-union to heal any
// self-intersections the vertex moves may have introduced.
ExPolygons relax_cone_ridges(const ExPolygons &eroded, double max_travel)
{
    Polygons relaxed;
    for (const ExPolygon &ex : eroded) {
        relaxed.emplace_back(curve_shorten_polygon(ex.contour, max_travel));
        for (const Polygon &hole : ex.holes)
            relaxed.emplace_back(curve_shorten_polygon(hole, max_travel));
    }
    return union_ex(relaxed);
}


void apply_conical_overhangs(const std::vector<Layer*> &layers,
                             double                     max_overhang_angle_deg,
                             double                     max_hole_area_mm2,
                             double                     extra_melt_angle_deg,
                             double                     subdivision_length_mm)
{
    if (layers.size() < 2)
        return;

    const bool relax_on = extra_melt_angle_deg > 0.;

    const double angle    = std::clamp(max_overhang_angle_deg, 0.0, 89.0) * pi / 180.0;
    const double tan_cone = std::tan(angle);

    // Outline of the layer above, already grown into the cone. `layers` is
    // ordered bottom..top, so we walk it top-down.
    ExPolygons grown_above = layers.back()->lslices;
    for (int i = int(layers.size()) - 2; i >= 0; -- i) {
        Layer       *layer = layers[i];
        const double dz    = layers[i + 1]->print_z - layer->print_z;
        if (dz <= 0.) {
            grown_above = layer->lslices;
            continue;
        }

        // Erode the grown outline above by the max printable horizontal step.
        // Eroding (not dilating) makes the cone narrow going downward at exactly
        // the overhang angle; where the model shrinks faster than that, the
        // eroded outline sticks out and becomes added support.
        const float step   = float(scale_(dz * tan_cone));
        ExPolygons  eroded = offset_ex(grown_above, -step);

        // Melt the cone's own ridges BEFORE unioning the model back in, so the
        // model's edges are never touched. Subdivide the eroded outline first so
        // the corner's turn has somewhere to spread (a bare convex polygon cannot
        // round), then apply a little curve-shortening flow. Compounded down the
        // stack, sharp ridges melt away completely.
        if (relax_on) {
            if (subdivision_length_mm > 0.) {
                const float seg = float(scale_(subdivision_length_mm));
                for (ExPolygon &ex : eroded) {
                    ex.contour.densify(seg);
                    for (Polygon &hole : ex.holes)
                        hole.densify(seg);
                }
            }
            // Cap the melt so no melted wall exceeds (overhang angle + extra):
            // the extra horizontal recession allowed per layer is
            // dz * (tan(base + extra) - tan(base)), clamped away from 90 deg.
            const double base_deg = std::clamp(max_overhang_angle_deg, 0.0, 89.0);
            const double top_deg  = std::min(base_deg + extra_melt_angle_deg, 89.0);
            const double max_travel = scale_(dz * (std::tan(top_deg * pi / 180.0) - tan_cone));
            if (max_travel > 0.)
                eroded = relax_cone_ridges(eroded, max_travel);
        }

        // Union the eroded/relaxed cone with this layer's own outline.
        Polygons all = to_polygons(eroded);
        append(all, to_polygons(layer->lslices));
        ExPolygons grown = union_ex(all);

        // Clean up before carrying the outline to the next layer: the repeated
        // erode/union accumulates arc vertices and spawns sub-extrusion-width
        // slivers (especially where holes grow and pinch off). Left unchecked
        // these explode the island count and choke G-code ordering. Drop tiny
        // islands and simplify contours to slicing resolution.
        //
        // Pipeline hazard: when ridge rounding is on, the ridge fillets and the
        // subdivision points are near-collinear and the 0.02 mm simplify would
        // strip them before they carry down the stack, undoing the rounding. Use
        // a much finer tolerance in that case (the subdivision already bounds the
        // vertex count), so fillets survive but true noise is still dropped.
        {
            const double min_area = scale_(0.3) * scale_(0.3);
            const double simplify_tol = relax_on ? scale_(0.002) : scale_(0.02);
            ExPolygons   cleaned;
            cleaned.reserve(grown.size());
            for (const ExPolygon &ex : grown)
                if (std::abs(ex.area()) > min_area)
                    append(cleaned, ex.simplify(simplify_tol));
            grown = std::move(cleaned);
        }

        // Keep large internal cavities open: the top-down union fills any hole
        // whose ceiling is solid above, which would solidify designed cavities.
        // Subtract the model's holes that exceed the threshold back out of the
        // grown outline so only small holes (< threshold) get filled/supported.
        {
            Polygons keep_open;
            for (const ExPolygon &ex : layer->lslices)
                for (const Polygon &hole : ex.holes)
                    if (std::abs(hole.area()) * SCALING_FACTOR * SCALING_FACTOR > max_hole_area_mm2)
                        keep_open.emplace_back(hole);
            if (! keep_open.empty())
                grown = diff_ex(grown, keep_open);
        }

        // Merge the cone into the layer outline. For a single region we replace
        // its slices with the unioned outline so the model and the cone form ONE
        // contour (one shell) instead of two abutting ones - abutting contours
        // would produce a doubled shell and a mass of sliver perimeters that
        // choke G-code generation. Multi-region keeps other regions and gives
        // the extra material to the first region.
        if (! layer->regions().empty()) {
            LayerRegion *lr = layer->regions().front();
            if (layer->regions().size() == 1) {
                lr->m_slices.clear();
                lr->m_slices.append(grown, stInternal);
            } else {
                ExPolygons added = diff_ex(grown, layer->lslices);
                if (! added.empty())
                    lr->m_slices.append(std::move(added), stInternal);
            }
            // Rebuild lslices AND lslice_indices_sorted_by_print_order from the
            // region slices. Overwriting lslices by hand left the print-order
            // index vector stale, which corrupted the G-code island ordering.
            layer->make_slices();
        }

        grown_above = std::move(grown);
    }
}

} // namespace Slic3r
