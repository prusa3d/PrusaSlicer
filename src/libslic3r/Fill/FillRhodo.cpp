///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <algorithm>
#include <cmath>
#include <vector>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "FillRhodo.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

// Constants similar to the python reference.
static inline double rhodo_deg_to_rad(double deg) { return deg * M_PI / 180.0; }
static constexpr double kSqrt3 = 1.7320508075688772935; // sqrt(3)

// Generate one layer of the Rhodo pattern inside a generic rectangular grid box, then clip.
static Polylines make_rhodo_layer(double z_mm, double spacing, const BoundingBox &grid_bb)
{
    // Map from Python variables:
    // l: hex edge length. Use spacing as effective line spacing; scale hex size with spacing.
    const double l = spacing; // Treat extrusion spacing as hexagon edge length basis.
    const double w = l * kSqrt3; // hex width

    // Compute phase across 3*l in Z.
    double z_phase = std::fmod(z_mm / l, 3.0);

    // Determine triangle fraction [0,1] and permutation bit per reference phases.
    int permutation = 0;
    double tri_frac = 0.0;
    if (z_phase < 1.0) {
        tri_frac = 0.0; // phase 1 hex hold
    } else if (z_phase < 1.25) {
        tri_frac = (z_phase - 1.0) * 4.0; // phase 2 transition
    } else if (z_phase < 1.5) {
        tri_frac = (1.5 - z_phase) * 4.0; // phase 3 reverse transition
        permutation = 1;
    } else if (z_phase < 2.5) {
        tri_frac = 0.0; // phase 4 hex hold
        permutation = 1;
    } else if (z_phase < 2.75) {
        tri_frac = (z_phase - 2.5) * 4.0; // phase 5 transition
        permutation = 1;
    } else {
        tri_frac = (3.0 - z_phase) * 4.0; // phase 6 reverse transition
    }
    tri_frac = std::clamp(tri_frac, 0.0, 1.0);

    const double tri_w      = w * tri_frac;    // width of the triangle caps
    const double tri_half_w = 0.5 * tri_w;

    // Grid size in mm from bounding box.
    const double width_mm  = unscale<double>(grid_bb.size()(0));
    const double height_mm = unscale<double>(grid_bb.size()(1));

    // Compute number of rows/cols to cover grid.
    const int num_rows = 2 + int(std::ceil(height_mm / (l + l / 2.0)));
    const int num_cols = 2 + int(std::ceil(width_mm  / w));

    // Helper to convert mm coords to scaled Point.
    auto to_point = [&](double x_mm, double y_mm) -> Point {
        // Offset by grid origin (grid_bb.min) and scale.
        return Point(scale_(x_mm) + grid_bb.min.x(), scale_(y_mm) + grid_bb.min.y());
    };

    Polylines polylines;
    polylines.reserve(size_t(num_rows * num_cols));

    // The python draws a single connected stroke per row; we replicate as polylines.
    if (permutation == 0) {
        for (int i = 0; i < num_rows; ++i) {
            const double row_offset = (i - 1) * (l + l / 2.0);
            Polyline pl;
            if ((i % 2) == 0) {
                for (int j = 0; j < num_cols; ++j) {
                    const double col_offset = j * w;
                    // Follow reference point sequence.
                    // top_left_tri_origin
                    const double tlx = col_offset - w / 2.0;
                    const double tly = row_offset - l / 2.0;
                    pl.points.emplace_back(to_point(tlx + tri_half_w, tly + tri_half_w / kSqrt3));
                    // hex top left
                    pl.points.emplace_back(to_point(col_offset, row_offset));
                    // left tri origin at bottom left vertex of hex
                    const double llx = col_offset;
                    const double lly = row_offset + l;
                    // left tri top
                    pl.points.emplace_back(to_point(llx, lly - tri_w * kSqrt3 / 3.0));
                    // left tri left
                    pl.points.emplace_back(to_point(llx - tri_half_w, lly + tri_half_w / kSqrt3));
                    if (j == num_cols - 1) break;
                    // left tri right
                    pl.points.emplace_back(to_point(llx + tri_half_w, lly + tri_half_w / kSqrt3));
                    // left tri top
                    pl.points.emplace_back(to_point(llx, lly - tri_w * kSqrt3 / 3.0));
                    // hex top left
                    pl.points.emplace_back(to_point(col_offset, row_offset));
                    // top tri origin (hex top center)
                    const double tcx = col_offset + w / 2.0;
                    const double tcy = row_offset - l / 2.0;
                    // top tri left
                    pl.points.emplace_back(to_point(tcx - tri_half_w, tcy + tri_half_w / kSqrt3));
                }
            } else {
                for (int j = num_cols - 1; j >= 0; --j) {
                    const double col_offset = j * w - w / 2.0;
                    // hex top right
                    pl.points.emplace_back(to_point(col_offset, row_offset));
                    // right tri top
                    const double rtx = col_offset;
                    const double rty = row_offset + l;
                    pl.points.emplace_back(to_point(rtx, rty - tri_w * kSqrt3 / 3.0));
                    // right tri right
                    pl.points.emplace_back(to_point(rtx + tri_half_w, rty + tri_half_w / kSqrt3));
                    if (j == 0) break;
                    // right tri left
                    pl.points.emplace_back(to_point(rtx - tri_half_w, rty + tri_half_w / kSqrt3));
                    // right tri top
                    pl.points.emplace_back(to_point(rtx, rty - tri_w * kSqrt3 / 3.0));
                    // hex top right
                    pl.points.emplace_back(to_point(col_offset, row_offset));
                    // top tri origin (hex top center)
                    const double tcx = col_offset - w / 2.0;
                    const double tcy = row_offset - l / 2.0;
                    // top tri right
                    pl.points.emplace_back(to_point(tcx + tri_half_w, tcy + tri_half_w / kSqrt3));
                    // top tri left (close the cap)
                    pl.points.emplace_back(to_point(tcx - tri_half_w, tcy + tri_half_w / kSqrt3));
                }
            }
            if (pl.size() > 1)
                polylines.emplace_back(std::move(pl));
        }
    } else {
        for (int i = 0; i < num_rows; ++i) {
            const double row_offset = (i - 1) * (l + l / 2.0) + l / 2.0;
            Polyline pl;
            if ((i % 2) == 0) {
                for (int j = 0; j < num_cols; ++j) {
                    const double col_offset = j * w - w / 2.0;
                    // left tri origin (hex top left)
                    const double lox = col_offset;
                    const double loy = row_offset;
                    // left tri right
                    pl.points.emplace_back(to_point(lox + tri_half_w, loy - tri_half_w / kSqrt3));
                    // left tri bot
                    pl.points.emplace_back(to_point(lox, loy + tri_w * kSqrt3 / 3.0));
                    // hex bottom left
                    pl.points.emplace_back(to_point(col_offset, row_offset + l));
                    // bot tri origin (hex bottom center)
                    const double bcx = col_offset + w / 2.0;
                    const double bcy = row_offset + 3.0 * l / 2.0;
                    // bot tri left
                    pl.points.emplace_back(to_point(bcx - tri_half_w, bcy - tri_half_w / kSqrt3));
                    if (j == num_cols - 1) break;
                    // bot tri right
                    pl.points.emplace_back(to_point(bcx + tri_half_w, bcy - tri_half_w / kSqrt3));
                    // hex bottom right
                    pl.points.emplace_back(to_point(col_offset + w, row_offset + l));
                    // right tri origin (hex top right)
                    const double rox = col_offset + w;
                    const double roy = row_offset;
                    // right tri bot
                    pl.points.emplace_back(to_point(rox, roy + tri_w * kSqrt3 / 3.0));
                    // right tri left
                    pl.points.emplace_back(to_point(rox - tri_half_w, roy - tri_half_w / kSqrt3));
                }
            } else {
                for (int j = num_cols - 1; j >= 0; --j) {
                    const double col_offset = j * w;
                    // right tri origin (hex top right)
                    const double rox = col_offset;
                    const double roy = row_offset;
                    // right tri left
                    pl.points.emplace_back(to_point(rox - tri_half_w, roy - tri_half_w / kSqrt3));
                    // right tri bot
                    pl.points.emplace_back(to_point(rox, roy + tri_w * kSqrt3 / 3.0));
                    // hex bottom right
                    pl.points.emplace_back(to_point(col_offset, row_offset + l));
                    // bot tri origin (hex bottom center)
                    const double bcx = col_offset - w / 2.0;
                    const double bcy = row_offset + 3.0 * l / 2.0;
                    // bot tri right
                    pl.points.emplace_back(to_point(bcx + tri_half_w, bcy - tri_half_w / kSqrt3));
                    if (j == 0) break;
                    // bot tri left
                    pl.points.emplace_back(to_point(bcx - tri_half_w, bcy - tri_half_w / kSqrt3));
                    // hex bottom left
                    pl.points.emplace_back(to_point(col_offset - w, row_offset + l));
                    // left tri origin (hex top left)
                    const double lox = col_offset - w;
                    const double loy = row_offset;
                    // left tri bot
                    pl.points.emplace_back(to_point(lox, loy + tri_w * kSqrt3 / 3.0));
                    // left tri right
                    pl.points.emplace_back(to_point(lox + tri_half_w, loy - tri_half_w / kSqrt3));
                }
            }
            if (pl.size() > 1)
                polylines.emplace_back(std::move(pl));
        }
    }

    return polylines;
}

void FillRhodo::_fill_surface_single(
	const FillParams                &params,
	unsigned int                     /*thickness_layers*/,
	const std::pair<float, Point>   &direction,
	ExPolygon                        expolygon,
	Polylines                       &polylines_out)
{
    // Rotate to align with infill direction.
    const float angle = direction.first;
    if (std::abs(angle) >= EPSILON)
        expolygon.rotate(-angle);

    // Work in rotated frame.
    BoundingBox bb = expolygon.contour.bounding_box();

    // Distance between pattern rows; adjust by density similar to line-based fills.
    const double density = std::max(0.0001, double(params.density));
    const coord_t distance = coord_t(scale_(this->spacing) / density);

    // Align to grid on multiples of pattern fundamental width/height. Use hex cell dims.
    const coord_t w_scaled = coord_t(kSqrt3 * distance); // scaled width of hex
    const coord_t h_step   = coord_t((3.0 / 2.0) * unscale<double>(distance) * scale_(1.0)); // approximate vertical stride in scaled units
    bb.merge(align_to_grid(bb.min, Point(w_scaled, h_step)));

    // Generate pattern lines over grid and clip.
    Polylines polylines = make_rhodo_layer(this->z, this->spacing, bb);

    polylines = intersection_pl(std::move(polylines), expolygon);

    if (!polylines.empty()) {
        const double minlength = scale_(0.5 * this->spacing);
        polylines.erase(
            std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
            polylines.end());
    }

    if (!polylines.empty()) {
        size_t first_idx = polylines_out.size();
        if (params.dont_connect())
            append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        if (std::abs(angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + first_idx; it != polylines_out.end(); ++it)
                it->rotate(angle);
        }
    }
}

} // namespace Slic3r


