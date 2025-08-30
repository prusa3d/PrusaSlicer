#include <algorithm>
#include <cmath>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "FillRhodo.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

void FillRhodo::_fill_surface_single(
	const FillParams              &params,
	unsigned int                   thickness_layers,
	const std::pair<float, Point> &direction,
	ExPolygon                      expolygon,
	Polylines                     &polylines_out)
{
	const coord_t min_spacing      = coord_t(scale_(this->spacing));
	const coord_t min_spacing_half = coord_t(scale_(this->spacing)) / 2;
	const coord_t hex_side         = coord_t(min_spacing / params.density);
	const coord_t hex_width        = coord_t(hex_side * sqrt(3));
	const coord_t tile_height      = coord_t(hex_side * 3) + min_spacing * 2; // tilable / periodic every other row
	const coord_t pattern_height   = tile_height / 2; // pattern height = hex_side * 1.5
	const Point   hex_center       = Point(hex_width / 2, hex_side);

	// Compute normalized z phase in [0, 4.5) relative to hex_side, using double for phase only.
	const double  unscaled_z = this->z; // mm
	const double  unscaled_hex_side = this->spacing / params.density;
	const double  z_phase = std::fmod(unscaled_z / unscaled_hex_side, 4.5);

	int    permutation = 0; // 0: upright triangles, 1: upside-down
	double tri_frac = 0.0;
	coord_t phase_y_offset = 0;
	coord_t phase_x_offset = 0;
	if (z_phase < 1.0) {
		tri_frac = 0.0;
	} else if (z_phase < 1.25) {
		tri_frac = (z_phase - 1.0) * 4.0;
	} else if (z_phase < 1.5) {
		tri_frac = (1.5 - z_phase) * 4.0;
		permutation = 1;
		phase_y_offset = hex_side / 2.0;
	} else if (z_phase < 2.5) {
		tri_frac = 0.0;
		permutation = 1;
		phase_y_offset = hex_side / 2.0;
	} else if (z_phase < 2.75) {
		tri_frac = (z_phase - 2.5) * 4.0;
		phase_y_offset = hex_side / 2.0;
		phase_x_offset = -hex_width / 2.0;
	} else if (z_phase < 3.0) {
		tri_frac = (3.0 - z_phase) * 4.0;
		permutation = 1;
		phase_y_offset = hex_side;
		phase_x_offset = -hex_width / 2.0;
	} else if (z_phase < 4.0) {
		tri_frac = 0.0;
		permutation = 1;
		phase_y_offset = hex_side;
		phase_x_offset = -hex_width / 2.0;
	} else if (z_phase < 4.25) {
		tri_frac = (z_phase - 4.0) * 4.0;
		phase_y_offset = hex_side;
	} else {
		tri_frac = (4.5 - z_phase) * 4.0;
		permutation = 1;
		phase_x_offset = -hex_width / 2.0;
	}

	const coord_t tri_w      = coord_t(std::llround(hex_width * tri_frac));
	const coord_t tri_half_w = tri_w / 2;

	// Work in a rotated frame aligned to infill direction.
	const float angle = direction.first;
	// Rotate expolygon by +angle to align pattern to axes, remember center used in other fills.
	// Using the same central reference as honeycomb to keep cross-layer alignment stable.
    BoundingBox bbox = expolygon.contour.bounding_box();
    {
        // align bounding box to a multiple of our honeycomb grid module
        Polygon bb_polygon = bbox.polygon();
        bb_polygon.rotate(angle, hex_center);
        bbox = bb_polygon.bounding_box();
        bbox.merge(align_to_grid(bbox.min, Point(hex_width + min_spacing, tile_height)));
    }

	// Expand bbox to cover entire surface with a margin of two cells as in reference.
	const coord_t w = bbox.max(0) - bbox.min(0);
	const coord_t h = bbox.max(1) - bbox.min(1);
	const size_t  num_rows = size_t(2 + (h + pattern_height - 1) / std::max<coord_t>(pattern_height, 1));
	const size_t  num_cols = size_t(2 + (w + hex_width - 1) / std::max<coord_t>(hex_width, 1));

	Polylines all_polylines;
	all_polylines.reserve(1);
	Polyline polyline;
	// Start one row above to guarantee coverage before
	const coord_t y_start = bbox.min(1) - pattern_height;
	for (size_t i = 0; i < num_rows; ++i) {
		// Row y origin in aligned grid frame
		coord_t y_offset = y_start + phase_y_offset + coord_t(i) * pattern_height;
		if (permutation == 0) {
			// phase with triangles at top-left/top-center transitions
			// even/odd rows alternate direction
			if ((i % 2) == 0) {
				for (size_t j = 0; j < num_cols; ++j) {
					coord_t x_offset = bbox.min(0) + phase_x_offset + coord_t(j) * hex_width;
					// top-left tri right
					polyline.points.emplace_back(x_offset - hex_width / 2 + tri_half_w, y_offset - hex_side / 2 + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// hex top left
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset);
					// left tri top
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset + coord_t(std::llround(double(hex_side) - double(tri_w) * sqrt(3) / 3.0)));
					// left tri left
					polyline.points.emplace_back(x_offset - tri_half_w, y_offset + hex_side + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					if (j + 1 == num_cols) break;
					// left tri right
					polyline.points.emplace_back(x_offset + tri_half_w, y_offset + hex_side + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// left tri top
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset + coord_t(std::llround(double(hex_side) - double(tri_w) * sqrt(3) / 3.0)));
					// hex top left
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset);
					// top tri left
					polyline.points.emplace_back(x_offset + hex_width / 2 - tri_half_w, y_offset - hex_side / 2 + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
				}
			} else {
				for (size_t j = num_cols; j-- > 0; ) {
					coord_t x_offset = bbox.min(0) + phase_x_offset + coord_t(j) * hex_width - hex_width / 2;
					// hex top right
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset);
					// right tri top
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset + coord_t(std::llround(double(hex_side) - double(tri_w) * sqrt(3) / 3.0)));
					// right tri right
					polyline.points.emplace_back(x_offset + tri_half_w, y_offset + hex_side + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					if (j == 0) break;
					// right tri left
					polyline.points.emplace_back(x_offset - tri_half_w, y_offset + hex_side + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// right tri top
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset + coord_t(std::llround(double(hex_side) - double(tri_w) * sqrt(3) / 3.0)));
					// hex top right
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset);
					// top tri right
					polyline.points.emplace_back(x_offset - hex_width / 2 + tri_half_w, y_offset - hex_side / 2 + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// top tri left
					polyline.points.emplace_back(x_offset - hex_width / 2 - tri_half_w, y_offset - hex_side / 2 + coord_t(std::llround(double(tri_half_w) / sqrt(3))));
				}
			}
		} else {
			if ((i % 2) == 0) {
				for (size_t j = 0; j < num_cols; ++j) {
					coord_t x_offset = bbox.min(0) + phase_x_offset + coord_t(j) * hex_width - hex_width / 2;
					// left tri right
					polyline.points.emplace_back(x_offset + tri_half_w, y_offset - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// left tri bottom
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset + coord_t(std::llround(double(tri_w) * sqrt(3) / 3.0)));
					// hex bottom left
					polyline.points.emplace_back(x_offset + min_spacing_half, y_offset + hex_side);
					// bottom tri left
					polyline.points.emplace_back(x_offset + hex_width / 2 - tri_half_w, y_offset + 3 * hex_side / 2 - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					if (j + 1 == num_cols) break;
					// bottom tri right
					polyline.points.emplace_back(x_offset + hex_width / 2 + tri_half_w, y_offset + 3 * hex_side / 2 - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// hex bottom right
					polyline.points.emplace_back(x_offset + hex_width - min_spacing_half, y_offset + hex_side);
					// right tri bottom
					polyline.points.emplace_back(x_offset + hex_width - min_spacing_half, y_offset + coord_t(std::llround(double(tri_w) * sqrt(3) / 3.0)));
					// right tri left
					polyline.points.emplace_back(x_offset + hex_width - tri_half_w, y_offset - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
				}
			} else {
				for (size_t j = num_cols; j-- > 0; ) {
					coord_t x_offset = bbox.min(0) + phase_x_offset + coord_t(j) * hex_width;
					// right tri left
					polyline.points.emplace_back(x_offset - tri_half_w, y_offset - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// right tri bottom
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset + coord_t(std::llround(double(tri_w) * sqrt(3) / 3.0)));
					// hex bottom right
					polyline.points.emplace_back(x_offset - min_spacing_half, y_offset + hex_side);
					// bottom tri right
					polyline.points.emplace_back(x_offset - hex_width / 2 + tri_half_w, y_offset + 3 * hex_side / 2 - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					if (j == 0) break;
					// bottom tri left
					polyline.points.emplace_back(x_offset - hex_width / 2 - tri_half_w, y_offset + 3 * hex_side / 2 - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
					// hex bottom left
					polyline.points.emplace_back(x_offset - hex_width + min_spacing_half, y_offset + hex_side);
					// left tri bottom
					polyline.points.emplace_back(x_offset - hex_width + min_spacing_half, y_offset + coord_t(std::llround(double(tri_w) * sqrt(3) / 3.0)));
					// left tri right
					polyline.points.emplace_back(x_offset - hex_width + tri_half_w, y_offset - coord_t(std::llround(double(tri_half_w) / sqrt(3))));
				}
			}
		}
	
	}

	all_polylines.emplace_back(std::move(polyline));

	// Clip to the surface polygon
	all_polylines = intersection_pl(std::move(all_polylines), expolygon);
	if (params.dont_connect() || all_polylines.size() <= 1)
		append(polylines_out, chain_polylines(std::move(all_polylines)));
	else
		connect_infill(std::move(all_polylines), expolygon, polylines_out, this->spacing, params);
}

} // namespace Slic3r
