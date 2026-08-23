///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationModels.hpp"
#include "TriangleMesh.hpp"
#include "MeshBoolean.hpp"
#include "ExPolygon.hpp"
#include "Tesselate.hpp"
#include "libslic3r.h"  // SCALING_FACTOR, scale_(), unscale<>()

#include <boost/log/trivial.hpp>

#include <cmath>
#include <string>
#include <unordered_map>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a right-triangle cross-section prism (wedge) for overhang cuts.
/// The right-angle corner sits at the origin; the hypotenuse runs from
/// (base_x, 0, 0) to (0, 0, height_z).  Extruded depth_y in +Y.
static indexed_triangle_set its_make_wedge(double base_x, double height_z, double depth_y)
{
    auto bx = float(base_x), hz = float(height_z), dy = float(depth_y);

    return {
        // 8 triangles: 2 end-caps + 3 quads (each split into 2 tris)
        {
            {0, 1, 2},            // front end-cap  (y = 0)
            {3, 5, 4},            // back  end-cap  (y = dy)
            {0, 3, 4}, {0, 4, 1}, // bottom quad    (z = 0)
            {1, 4, 5}, {1, 5, 2}, // hypotenuse quad
            {0, 2, 5}, {0, 5, 3}, // left side quad (x = 0)
        },
        {
            Vec3f(0, 0, 0),      // 0: front origin
            Vec3f(bx, 0, 0),     // 1: front base
            Vec3f(0, 0, hz),     // 2: front top
            Vec3f(0, dy, 0),     // 3: back origin
            Vec3f(bx, dy, 0),    // 4: back base
            Vec3f(0, dy, hz),    // 5: back top
        }
    };
}

// ---------------------------------------------------------------------------
// Block-letter text engraving (no font loading required)
// ---------------------------------------------------------------------------

/// Bitmap font for digits 0-9.  Each glyph is a 7-wide × 10-tall grid of
/// 0/1 values (row-major, top to bottom).  Used by make_block_text() to
/// create raised pixel-block labels on the temperature tower tiers.
static const uint8_t DIGIT_FONT[10][10][7] = {
    // 0
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,1,1,1},{1,1,0,1,0,1,1},{1,1,1,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 1
    {{0,0,0,1,0,0,0},{0,0,1,1,0,0,0},{0,1,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,1,1,1,1,1,0}},
    // 2
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,1,1,0},{0,0,0,1,1,0,0},{0,0,1,1,0,0,0},{0,1,1,0,0,0,0},{1,1,0,0,0,0,0},{1,1,0,0,0,1,1},{1,1,1,1,1,1,1}},
    // 3
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,1,1,1,1,0},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 4
    {{0,0,0,0,1,1,0},{0,0,0,1,1,1,0},{0,0,1,0,1,1,0},{0,1,0,0,1,1,0},{1,0,0,0,1,1,0},{1,1,1,1,1,1,1},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0}},
    // 5
    {{1,1,1,1,1,1,1},{1,1,0,0,0,0,0},{1,1,0,0,0,0,0},{1,1,1,1,1,1,0},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 6
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,0,0},{1,1,0,0,0,0,0},{1,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 7
    {{1,1,1,1,1,1,1},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,1,1,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0}},
    // 8
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 9
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
};

// Extra glyphs for -, +, %, . (same 7×10 grid)
static const uint8_t GLYPH_MINUS[10][7] = {
    {0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{1,1,1,1,1,1,1},{1,1,1,1,1,1,1},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0}};
static const uint8_t GLYPH_PLUS[10][7] = {
    {0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{1,1,1,1,1,1,1},{1,1,1,1,1,1,1},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0}};
static const uint8_t GLYPH_PERCENT[10][7] = {
    {1,1,0,0,0,0,0},{1,1,0,0,0,1,1},{0,0,0,0,1,1,0},{0,0,0,1,1,0,0},{0,0,0,1,0,0,0},{0,0,1,1,0,0,0},{0,1,1,0,0,0,0},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,0,0}};
static const uint8_t GLYPH_DOT[10][7] = {
    {0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,1,1,0,0,0,0},{0,1,1,0,0,0,0}};

static constexpr int GLYPH_W  = 7;   // pixels wide per glyph
static constexpr int GLYPH_H  = 10;  // pixels tall per glyph
static constexpr int GLYPH_SP = 2;   // pixel spacing between glyphs

/// Build a block-letter mesh for a numeric string.
/// Each lit pixel becomes a small cube; the whole label is centred at origin
/// in XZ, extruded depth_mm in +Y.  height_mm controls overall glyph height.
/// The mesh is pre-mirrored in X so it reads correctly after the 180° Z
/// rotation applied in make_temp_tower().
// Helper: get the glyph bitmap for a character.
// Returns pointer to a [10][7] array, or nullptr if unsupported.
static const uint8_t (*get_glyph(char c))[7]
{
    if (c >= '0' && c <= '9') return &DIGIT_FONT[c - '0'][0];
    if (c == '-')             return &GLYPH_MINUS[0];
    if (c == '+')             return &GLYPH_PLUS[0];
    if (c == '%')             return &GLYPH_PERCENT[0];
    if (c == '.')             return &GLYPH_DOT[0];
    return nullptr;
}

indexed_triangle_set make_block_text(const std::string& text, double height_mm, double depth_mm, bool mirror_x)
{
    // Collect supported glyphs
    std::vector<const uint8_t (*)[7]> glyphs;
    for (char c : text) {
        auto g = get_glyph(c);
        if (g) glyphs.push_back(g);
    }
    if (glyphs.empty())
        return {};

    double pixel_size = height_mm / GLYPH_H;
    int total_w = int(glyphs.size()) * GLYPH_W + (int(glyphs.size()) - 1) * GLYPH_SP;
    double total_width  = total_w * pixel_size;
    double total_height = GLYPH_H * pixel_size;

    indexed_triangle_set result;

    for (size_t gi = 0; gi < glyphs.size(); ++gi) {
        const auto& glyph = glyphs[gi];
        int x_offset_px = int(gi) * (GLYPH_W + GLYPH_SP);

        for (int row = 0; row < GLYPH_H; ++row) {
            for (int col = 0; col < GLYPH_W; ++col) {
                if (!glyph[row][col])
                    continue;

                auto block = its_make_cube(pixel_size, depth_mm, pixel_size);

                double x = (x_offset_px + col) * pixel_size - total_width / 2.0;
                double z = (GLYPH_H - 1 - row) * pixel_size - total_height / 2.0;

                its_translate(block, Vec3f(float(x), 0.f, float(z)));
                its_merge(result, block);
            }
        }
    }

    // Mirror in X so text reads correctly after the 180° Z rotation in make_temp_tower.
    // Callers that don't apply a 180° rotation should pass mirror_x=false.
    if (mirror_x) {
        for (auto& v : result.vertices)
            v.x() = -v.x();
        for (auto& f : result.indices)
            std::swap(f[0], f[1]);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Flow Specimen (serpentine / E-shape) — built as extruded 2D polygon
// ---------------------------------------------------------------------------

// PrusaSlicer's Polygon uses integer coord_t (nanometer scale).
// Use the standard scale_() macro to convert mm → coord_t.

/// Append n+1 arc points to a polygon.
/// Centre (cx, cy) and radius r are in mm; angles in radians.
/// The arc interpolates linearly from a_start to a_end (which may cross
/// ±2π for "long way around" arcs).
static void append_arc(Polygon& poly, double cx, double cy, double r,
                       double a_start, double a_end, int n = 32)
{
    for (int i = 0; i <= n; ++i) {
        double a = a_start + (a_end - a_start) * i / n;
        double x = cx + r * std::cos(a);
        double y = cy + r * std::sin(a);
        poly.points.push_back(Point(coord_t(scale_(x)), coord_t(scale_(y))));
    }
}

/// Extrude an ExPolygon into a closed 3D mesh from z=0 to z=height.
/// Side walls are built as quad strips from each contour edge.
/// Top and bottom caps use PrusaSlicer's GLU tessellator to correctly
/// triangulate concave polygons with holes.
///
/// All vertices are computed once via a canonical conversion (coord_t → float)
/// and shared between side walls and caps to avoid float-rounding mismatches
/// that cause near-degenerate facet warnings during mesh repair.
static indexed_triangle_set extrude_expolygon(const ExPolygon& expoly, double height)
{
    indexed_triangle_set result;

    // --- Phase 1: Create shared vertices and build a lookup table. ---
    // For each polygon point we create exactly two vertices (z=0 and z=height)
    // and record their indices so caps can reference them.
    //
    // vertex_base[point_index] gives the index into result.vertices for the
    // z=0 vertex; vertex_base[point_index] + total_pts gives the z=height one.

    // Flatten all contour points into a single list (outer contour first,
    // then holes) and record where each contour starts.
    std::vector<Vec3f>  flat_pts;       // canonical XY for each point
    std::vector<int>    contour_starts; // start index in flat_pts per contour
    std::vector<int>    contour_sizes;  // number of points per contour

    auto add_contour = [&](const Polygon& poly) {
        contour_starts.push_back(int(flat_pts.size()));
        contour_sizes.push_back(int(poly.points.size()));
        for (const Point& pt : poly.points) {
            float x = float(unscale<double>(pt.x()));
            float y = float(unscale<double>(pt.y()));
            flat_pts.push_back(Vec3f(x, y, 0.f));
        }
    };

    add_contour(expoly.contour);
    for (const auto& h : expoly.holes)
        add_contour(h);

    int total_pts = int(flat_pts.size());
    int base0 = int(result.vertices.size());

    // Bottom ring (z = 0)
    for (int i = 0; i < total_pts; ++i)
        result.vertices.push_back(flat_pts[i]);
    // Top ring (z = height)
    for (int i = 0; i < total_pts; ++i)
        result.vertices.push_back(Vec3f(flat_pts[i].x(), flat_pts[i].y(), float(height)));

    // --- Phase 2: Side walls ---
    for (size_t ci = 0; ci < contour_starts.size(); ++ci) {
        int start = contour_starts[ci];
        int n     = contour_sizes[ci];
        if (n < 3) continue;

        for (int i = 0; i < n; ++i) {
            int j  = (i + 1) % n;
            int b0 = base0 + start + i;
            int b1 = base0 + start + j;
            int t0 = base0 + total_pts + start + i;
            int t1 = base0 + total_pts + start + j;
            result.indices.push_back({b0, b1, t1});
            result.indices.push_back({b0, t1, t0});
        }
    }

    // --- Phase 3: Top and bottom caps ---
    // Tessellate the ExPolygon, then snap each returned vertex to the
    // nearest shared vertex to avoid duplicate-vertex precision issues.
    {
        ExPolygon ep = expoly;
        ep.contour.make_counter_clockwise();
        for (auto& hole : ep.holes)
            hole.make_clockwise();

        // Build a spatial hash for O(1) vertex lookup.
        // Key: quantised (x, y) packed into int64_t.  The quantisation
        // grid (0.001 mm) is finer than any rounding difference between
        // the tessellator and our canonical vertices.
        auto quantise = [](float v) -> int32_t { return int32_t(std::round(v * 1000.f)); };
        auto pack_key = [&](float x, float y) -> int64_t {
            return (int64_t(uint32_t(quantise(x))) << 32) | int64_t(uint32_t(quantise(y)));
        };

        std::unordered_map<int64_t, int> pt_index;
        pt_index.reserve(total_pts);
        for (int i = 0; i < total_pts; ++i)
            pt_index[pack_key(flat_pts[i].x(), flat_pts[i].y())] = i;

        // Look up the shared vertex index for a tessellated point.
        // z_offset selects bottom ring (0) or top ring (total_pts).
        auto find_nearest = [&](float x, float y, int z_offset) -> int {
            auto it = pt_index.find(pack_key(x, y));
            if (it != pt_index.end())
                return base0 + z_offset + it->second;
            // Fallback: linear scan (should not happen)
            int   best_idx  = base0 + z_offset;
            float best_dist = std::numeric_limits<float>::max();
            for (int i = 0; i < total_pts; ++i) {
                float dx = flat_pts[i].x() - x;
                float dy = flat_pts[i].y() - y;
                float d  = dx * dx + dy * dy;
                if (d < best_dist) { best_dist = d; best_idx = base0 + z_offset + i; }
            }
            return best_idx;
        };

        // Bottom face (flip = true for outward-facing-down normals)
        auto bot_tris = triangulate_expolygon_3d(ep, 0.0, true);
        for (size_t i = 0; i + 2 < bot_tris.size(); i += 3) {
            int i0 = find_nearest(float(bot_tris[i].x()),     float(bot_tris[i].y()),     0);
            int i1 = find_nearest(float(bot_tris[i + 1].x()), float(bot_tris[i + 1].y()), 0);
            int i2 = find_nearest(float(bot_tris[i + 2].x()), float(bot_tris[i + 2].y()), 0);
            result.indices.push_back({i0, i1, i2});
        }

        // Top face (flip = false for outward-facing-up normals)
        auto top_tris = triangulate_expolygon_3d(ep, 0.0, false);
        for (size_t i = 0; i + 2 < top_tris.size(); i += 3) {
            int i0 = find_nearest(float(top_tris[i].x()),     float(top_tris[i].y()),     total_pts);
            int i1 = find_nearest(float(top_tris[i + 1].x()), float(top_tris[i + 1].y()), total_pts);
            int i2 = find_nearest(float(top_tris[i + 2].x()), float(top_tris[i + 2].y()), total_pts);
            result.indices.push_back({i0, i1, i2});
        }
    }

    return result;
}

indexed_triangle_set make_flow_specimen(
    int    num_levels,
    double level_height,
    double width,
    double arm_thickness,
    double gap_width,
    int    num_arms)
{
    if (num_levels <= 0 || level_height <= 0.0 || num_arms < 1 || arm_thickness <= 0.0 || gap_width <= 0.0)
        return {};

    double depth  = num_arms * arm_thickness + (num_arms - 1) * gap_width;
    double height = num_levels * level_height;

    double inner_r = gap_width / 2.0;       // fillet radius for slot left ends
    double outer_r = arm_thickness / 2.0;    // fillet radius for arm tips (right side)
    double spine_r = arm_thickness - 0.5;    // fillet radius for spine corners (left side)

    double left_x  = -width / 2.0;          // left edge of bounding box
    double spine_x = left_x + arm_thickness; // right edge of spine (= left edge of slots)
    double bot_y   = -depth / 2.0;           // bottom of bounding box
    double top_y   =  depth / 2.0;           // top of bounding box

    // The cross-section is built as a SINGLE polygon (no holes) by tracing
    // the entire serpentine boundary CCW.  After each arm tip arc, the
    // contour dips into the slot gap — along the bottom of the slot, around
    // the left semicircle (into the spine), then back along the top — before
    // continuing to the next arm.  This avoids polygon-with-holes
    // tessellation artifacts that occur when hole edges coincide with outer
    // contour edges.
    //
    // For 3 arms the path is:
    //   spine bottom-left arc → arm 0 bottom → arm 0 tip arc →
    //   slot 0 bottom → slot 0 left semicircle → slot 0 top →
    //   arm 1 bottom → arm 1 tip arc →
    //   slot 1 bottom → slot 1 left semicircle → slot 1 top →
    //   arm 2 bottom → arm 2 tip arc →
    //   spine top-left arc → (closes back to start)

    double slot_left_cx = spine_x + inner_r;         // left semicircle centre X
    double slot_right_x = width / 2.0 - outer_r;     // arm tip arc centre X

    Polygon outer;

    // Bottom-left spine corner (180° → 270°)
    append_arc(outer, left_x + spine_r, bot_y + spine_r, spine_r, M_PI, 3.0 * M_PI / 2.0);

    for (int i = 0; i < num_arms; ++i) {
        double yb = bot_y + i * (arm_thickness + gap_width);
        double yt = yb + arm_thickness;
        double arm_cy = (yb + yt) / 2.0;

        // Straight bottom edge of arm, then semicircular tip (-90° → +90°)
        outer.points.push_back(Point(coord_t(scale_(slot_right_x)), coord_t(scale_(yb))));
        append_arc(outer, slot_right_x, arm_cy, outer_r, -M_PI / 2.0, M_PI / 2.0);

        if (i < num_arms - 1) {
            // Dip into the slot gap between arm i and arm i+1.
            double slot_cy = yt + gap_width / 2.0;
            double next_yb = yt + gap_width;

            // Bottom of slot (= top of arm i): right → left
            outer.points.push_back(Point(coord_t(scale_(slot_left_cx)), coord_t(scale_(yt))));

            // Left semicircle: -90° → +90° the LONG way through 180°,
            // i.e. -90° → -270° so the arc curves left into the spine.
            append_arc(outer, slot_left_cx, slot_cy, inner_r, -M_PI / 2.0, -3.0 * M_PI / 2.0);

            // Top of slot (= bottom of arm i+1): left → right
            outer.points.push_back(Point(coord_t(scale_(slot_right_x)), coord_t(scale_(next_yb))));
        }
    }

    // Top-left spine corner (90° → 180°)
    append_arc(outer, left_x + spine_r, top_y - spine_r, spine_r, M_PI / 2.0, M_PI);
    // Left edge closes implicitly back to the start point.

    ExPolygon expoly;
    expoly.contour = outer;

    return extrude_expolygon(expoly, height);
}

// ---------------------------------------------------------------------------
// Temperature Tower
// ---------------------------------------------------------------------------
// Each tier is a 79×10×10 mm block with test features: 45°/35° overhangs,
// a central bridge cutout with small and large cones, vertical and horizontal
// holes, a surface protrusion bar, and a raised block-letter temperature label.
// Tiers are stacked on a 89.3×20×1 mm base plate.  Geometry constants below
// are in millimetres and match the original Python temp_model.py.

// Base plate dimensions
static constexpr double BASE_LENGTH     = 89.3;
static constexpr double BASE_WIDTH      = 20.0;
static constexpr double BASE_HEIGHT     = TEMP_TOWER_BASE_HEIGHT;

// Tier block dimensions
static constexpr double TIER_LENGTH     = 79.0;
static constexpr double TIER_WIDTH      = 10.0;
static constexpr double TIER_HEIGHT     = TEMP_TOWER_TIER_HEIGHT;

// Overhang wedge X-extents (left = 45°, right = 35°)
static constexpr double OVERHANG_45_X   = 10.0;
static constexpr double OVERHANG_35_X   = 14.281;  // tan(35°) × TIER_HEIGHT

// Central rectangular cutout (bridge test area)
static constexpr double CUTOUT_LENGTH   = 30.0;
static constexpr double CUTOUT_HEIGHT   = 9.0;
static constexpr double CUTOUT_OFFSET   = 15.0;    // X offset from tier origin

// Cones inside the cutout (stringing/detail test)
static constexpr double CONE_HEIGHT     = 5.0;
static constexpr double SM_CONE_DIAM    = 3.0;
static constexpr double SM_CONE_OFFSET  = 5.0;     // from cutout start
static constexpr double LG_CONE_DIAM    = 5.0;
static constexpr double LG_CONE_OFFSET  = 25.0;    // from cutout start

// Vertical and horizontal holes (bridging/overhang test)
static constexpr double HOLE_DIAM       = 3.0;
static constexpr double HOLE_45_OFFSET  = 3.671;   // near 45° overhang
static constexpr double HOLE_35_OFFSET  = 75.0;    // near 35° overhang
static constexpr double HORIZ_HOLE_LEN  = 5.0;     // horizontal hole length

// Surface protrusion bar (layer adhesion test)
static constexpr double PROTRUSION_LENGTH = 16.0;
static constexpr double PROTRUSION_HEIGHT = 0.7;
static constexpr double PROTRUSION_DEPTH  = 0.5;
static constexpr double TEST_CUTOUT_H_OFFSET = 47.0;
static constexpr double TEST_CUTOUT_V_OFFSET = 0.3;

// Block-letter temperature label (raised on back face)
static constexpr double TEMP_LABEL_SIZE     = 6.0;   // mm font height
static constexpr double TEMP_LABEL_DEPTH    = 0.6;   // mm label thickness
static constexpr double TEMP_LABEL_V_OFFSET = 5.0;   // mm from tier bottom
static constexpr double TEMP_LABEL_H_OFFSET = 25.0;  // mm from tier right edge

/// Build a single tier with all test features at local origin.
/// temperature > 0 adds a raised block-letter label on the back face.
static indexed_triangle_set make_tier(int temperature)
{
    auto tier = its_make_cube(TIER_LENGTH, TIER_WIDTH, TIER_HEIGHT);

    // 45-degree overhang cut (left side)
    {
        auto wedge = its_make_wedge(OVERHANG_45_X, TIER_HEIGHT, TIER_WIDTH);
        MeshBoolean::cgal::minus(tier, wedge);
    }

    // 35-degree overhang cut (right side)
    {
        auto wedge = its_make_wedge(OVERHANG_35_X, TIER_HEIGHT, TIER_WIDTH);
        for (auto& v : wedge.vertices)
            v.x() = -v.x();
        its_translate(wedge, Vec3f(float(TIER_LENGTH), 0.f, 0.f));
        for (auto& f : wedge.indices)
            std::swap(f[0], f[1]);
        MeshBoolean::cgal::minus(tier, wedge);
    }

    // Central rectangular cutout
    {
        auto cutout = its_make_cube(CUTOUT_LENGTH, TIER_WIDTH + 2.0, CUTOUT_HEIGHT);
        its_translate(cutout, Vec3f(float(CUTOUT_OFFSET), -1.f, 0.f));
        MeshBoolean::cgal::minus(tier, cutout);
    }

    // Vertical hole near 45-degree overhang
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, TIER_HEIGHT + 2.0);
        its_translate(hole, Vec3f(float(HOLE_45_OFFSET), float(TIER_WIDTH / 2.0), -1.f));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Vertical hole near 35-degree overhang
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, TIER_HEIGHT + 2.0);
        its_translate(hole, Vec3f(float(HOLE_35_OFFSET), float(TIER_WIDTH / 2.0), -1.f));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Horizontal hole through the bridge area
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, HORIZ_HOLE_LEN);
        Transform3d rot = Transform3d::Identity();
        rot.rotate(Eigen::AngleAxisd(M_PI / 2.0, Vec3d::UnitX()));
        its_transform(hole, rot);
        its_translate(hole, Vec3f(
            float(CUTOUT_OFFSET + CUTOUT_LENGTH),
            float(TIER_WIDTH),
            float(TIER_HEIGHT / 2.0)));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Temperature label raised on back face
    if (temperature > 0) {
        auto text_mesh = make_block_text(std::to_string(temperature), TEMP_LABEL_SIZE, TEMP_LABEL_DEPTH);
        if (!text_mesh.empty()) {
            its_translate(text_mesh, Vec3f(
                float(TIER_LENGTH - TEMP_LABEL_H_OFFSET),
                float(TIER_WIDTH),
                float(TEMP_LABEL_V_OFFSET)));
            its_merge(tier, text_mesh);
        }
    }

    // Small cone inside cutout
    {
        auto cone = its_make_cone(SM_CONE_DIAM / 2.0, CONE_HEIGHT);
        its_translate(cone, Vec3f(
            float(CUTOUT_OFFSET + SM_CONE_OFFSET),
            float(TIER_WIDTH / 2.0),
            0.f));
        its_merge(tier, cone);
    }

    // Large cone inside cutout
    {
        auto cone = its_make_cone(LG_CONE_DIAM / 2.0, CONE_HEIGHT);
        its_translate(cone, Vec3f(
            float(CUTOUT_OFFSET + LG_CONE_OFFSET),
            float(TIER_WIDTH / 2.0),
            0.f));
        its_merge(tier, cone);
    }

    // Test protrusion bar on front face
    {
        auto bar = its_make_cube(PROTRUSION_LENGTH, PROTRUSION_DEPTH, PROTRUSION_HEIGHT);
        its_translate(bar, Vec3f(
            float(TEST_CUTOUT_H_OFFSET),
            float(-PROTRUSION_DEPTH),
            float(TEST_CUTOUT_V_OFFSET)));
        its_merge(tier, bar);
    }

    return tier;
}

indexed_triangle_set make_temp_tower(int num_tiers, int start_temp, int temp_step)
{
    // Base plate centred at XY origin
    auto tower = its_make_cube(BASE_LENGTH, BASE_WIDTH, BASE_HEIGHT);
    its_translate(tower, Vec3f(float(-BASE_LENGTH / 2.0), float(-BASE_WIDTH / 2.0), 0.f));

    // Stack tiers from bottom (hottest) to top (coolest)
    for (int i = 0; i < num_tiers; ++i) {
        int temperature = start_temp - i * temp_step;
        auto tier = make_tier(temperature);

        // Centre tier on base plate, stack at correct Z height
        its_translate(tier, Vec3f(
            float(-TIER_LENGTH / 2.0),
            float(-TIER_WIDTH / 2.0),
            float(BASE_HEIGHT + i * TIER_HEIGHT)));

        try {
            MeshBoolean::cgal::plus(tower, tier);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "CalibrationModels: CGAL union failed for tier "
                                       << i << ": " << e.what() << " — falling back to merge";
            its_merge(tower, tier);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "CalibrationModels: CGAL union failed for tier "
                                       << i << " — falling back to merge";
            its_merge(tower, tier);
        }
    }

    // Rotate 180° around Z so temperature labels face the front (-Y)
    Transform3d rot = Transform3d::Identity();
    rot.rotate(Eigen::AngleAxisd(M_PI, Vec3d::UnitZ()));
    its_transform(tower, rot);

    return tower;
}

// ---------------------------------------------------------------------------
// Pressure Advance Chevron Pattern
// ---------------------------------------------------------------------------
// Generates a single V-shaped chevron inside a rectangular frame for
// pressure advance calibration.  The model is extruded to
// num_layers × layer_height.  Each group of layers gets a different PA
// value via per-layer custom G-code inserted by the dialog.
//
// Geometry (viewed from above, chevron pointing right):
//
//        outer tip
//       /         \
//      /   inner   \       Each arm is wall_thickness wide.
//     /    notch    \
//    /_______________\
//    arm endpoint      arm endpoint

/// Compute the 6-vertex polygon for a single thick chevron (V-shape).
/// tip_x: X position of the chevron tip.  The chevron points right (+X).
/// Returns vertices CCW suitable for extrusion.
static std::vector<Vec2d> chevron_outline(
    double tip_x, double arm_length, double corner_angle_deg, double wall_thickness)
{
    double half = corner_angle_deg * M_PI / 360.0;  // half angle in radians
    double cos_a = std::cos(half);
    double sin_a = std::sin(half);
    double hw = wall_thickness / 2.0;

    // Centre-line arm endpoint (left of tip)
    double lx = tip_x - arm_length * cos_a;
    double ly = arm_length * sin_a;

    // Outer and inner tip X (intersection of offset arm edges)
    double outer_tip_x = tip_x + hw / sin_a;
    double inner_tip_x = tip_x - hw / sin_a;

    // Arm endpoint offsets (perpendicular to arm direction)
    double top_outer_lx = lx + hw * sin_a;
    double top_outer_ly = ly + hw * cos_a;
    double top_inner_lx = lx - hw * sin_a;
    double top_inner_ly = ly - hw * cos_a;

    // Bottom arm (mirror Y)
    double bot_inner_lx = lx - hw * sin_a;
    double bot_inner_ly = -(ly - hw * cos_a);
    double bot_outer_lx = lx + hw * sin_a;
    double bot_outer_ly = -(ly + hw * cos_a);

    // CCW winding
    return {
        {outer_tip_x,  0.0},           // outer tip (rightmost)
        {top_outer_lx, top_outer_ly},   // outer top-left
        {top_inner_lx, top_inner_ly},   // inner top-left
        {inner_tip_x,  0.0},           // inner tip (notch)
        {bot_inner_lx, bot_inner_ly},   // inner bottom-left
        {bot_outer_lx, bot_outer_ly},   // outer bottom-left
    };
}

/// Simple prism extrusion of a 2D polygon (no holes).
/// Vertices are in XY, extruded from z=0 to z=height.
static indexed_triangle_set extrude_polygon(const std::vector<Vec2d>& pts, double height)
{
    indexed_triangle_set result;
    int n = int(pts.size());
    if (n < 3) return result;

    // Bottom and top vertex rings
    for (int i = 0; i < n; ++i)
        result.vertices.push_back(Vec3f(float(pts[i].x()), float(pts[i].y()), 0.f));
    for (int i = 0; i < n; ++i)
        result.vertices.push_back(Vec3f(float(pts[i].x()), float(pts[i].y()), float(height)));

    // Side quads
    for (int i = 0; i < n; ++i) {
        int j  = (i + 1) % n;
        int b0 = i, b1 = j, t0 = n + i, t1 = n + j;
        result.indices.push_back({b0, b1, t1});
        result.indices.push_back({b0, t1, t0});
    }

    // Bottom cap (fan from vertex 0, reversed winding for downward normal)
    for (int i = 1; i + 1 < n; ++i)
        result.indices.push_back({0, i + 1, i});

    // Top cap (fan from vertex n, normal winding for upward normal)
    for (int i = 1; i + 1 < n; ++i)
        result.indices.push_back({n, n + i, n + i + 1});

    return result;
}

indexed_triangle_set make_pa_pattern(
    int    num_layers,
    double layer_height,
    double corner_angle,
    double arm_length,
    double wall_thickness)
{
    if (num_layers <= 0 || layer_height <= 0.0 || corner_angle <= 0.0 || corner_angle >= 180.0 ||
        arm_length <= 0.0 || wall_thickness <= 0.0)
        return {};

    double height = num_layers * layer_height;
    double half   = corner_angle * M_PI / 360.0;
    double sin_a  = std::sin(half);
    double cos_a  = std::cos(half);
    double hw     = wall_thickness / 2.0;

    // Single chevron centred at X = 0
    auto verts  = chevron_outline(0.0, arm_length, corner_angle, wall_thickness);
    auto result = extrude_polygon(verts, height);

    // Rectangular frame enclosing the chevron (1 layer tall)
    {
        double arm_lx = -arm_length * cos_a;
        double x_min  = arm_lx - hw * sin_a - 1.0;  // small margin
        double x_max  = hw / sin_a + 1.0;

        double y_extent = arm_length * sin_a + hw * cos_a;
        double y_min = -y_extent - 1.0;
        double y_max =  y_extent + 1.0;

        double frame_h = layer_height;  // 1 layer tall

        // Outer box
        auto outer = its_make_cube(x_max - x_min, y_max - y_min, frame_h);
        its_translate(outer, Vec3f(float(x_min), float(y_min), 0.f));

        // Inner cutout
        double w = wall_thickness;
        auto inner = its_make_cube(
            x_max - x_min - 2 * w, y_max - y_min - 2 * w, frame_h + 2.0);
        its_translate(inner, Vec3f(float(x_min + w), float(y_min + w), -1.f));

        MeshBoolean::cgal::minus(outer, inner);
        its_merge(result, outer);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Retraction Calibration Towers
// ---------------------------------------------------------------------------

indexed_triangle_set make_retraction_towers(
    double height, double diameter, double spacing)
{
    static constexpr double BASE_HEIGHT  = 1.0;  // mm

    if (height <= BASE_HEIGHT || diameter <= 0.0 || spacing <= 0.0)
        return {};

    double radius = diameter / 2.0;

    // Rectangular base: spans both towers with margin
    static constexpr double BASE_MARGIN  = 5.0;  // mm padding around towers
    double base_width  = spacing + diameter + 2.0 * BASE_MARGIN;
    double base_depth  = diameter + 2.0 * BASE_MARGIN;

    auto base = its_make_cube(base_width, base_depth, BASE_HEIGHT);
    its_translate(base, Vec3f(float(-base_width / 2.0), float(-base_depth / 2.0), 0.f));

    // Towers sit on top of the base
    double fa = 2.0 * M_PI / 64.0;
    auto tower1 = its_make_cylinder(radius, height - BASE_HEIGHT, fa);
    its_translate(tower1, Vec3f(float(-spacing / 2.0), 0.f, float(BASE_HEIGHT)));

    auto tower2 = its_make_cylinder(radius, height - BASE_HEIGHT, fa);
    its_translate(tower2, Vec3f(float(spacing / 2.0), 0.f, float(BASE_HEIGHT)));

    its_merge(base, tower1);
    its_merge(base, tower2);
    return base;
}

// ---------------------------------------------------------------------------
// Shrinkage / Dimensional Accuracy Gauge
// ---------------------------------------------------------------------------
//
// Three 10×10mm bars extending from a common corner along X, Y, and Z.
// 1mm-wide, 1mm-deep grooves cut at 25mm intervals on two visible faces
// of each arm for caliper measurement reference.

static constexpr double BAR_SECTION     = 10.0;  // mm — cross-section of each arm
// ---------------------------------------------------------------------------
// Fan Speed Test Tower
// ---------------------------------------------------------------------------
// Inspired by "Ultimate Fan Test Tower v2": two vertical columns connected
// by horizontal overhang shelves at each level, on a base plate.
// Tests bridging and overhang quality at different fan speeds.

static constexpr double FAN_BASE_D       = 20.0;  // mm — base plate depth
static constexpr double FAN_BASE_H       = 1.0;   // mm — base plate height
static constexpr double FAN_COL_DIAM     = 5.0;   // mm — column diameter
static constexpr double FAN_COL_SPACING  = 45.0;  // mm — column center-to-center (narrowed by 5mm)
static constexpr double FAN_SHELF_THICK  = 2.0;   // mm — overhang shelf thickness
static constexpr double FAN_SHELF_DEPTH  = FAN_COL_DIAM;  // mm — shelf depth = column diameter
static constexpr double FAN_SHELF_EXTRA  = 0.0;   // mm — flush with columns (no overhang past)
static constexpr double FAN_LABEL_SIZE   = 4.0;   // mm — font height for labels

indexed_triangle_set make_fan_tower(int num_levels)
{
    double total_height = FAN_BASE_H + num_levels * FAN_TOWER_LEVEL_HEIGHT;
    double col_r = FAN_COL_DIAM / 2.0;

    indexed_triangle_set tower;

    // Standalone cylinder X position (computed early so base plate can cover it)
    double col_x_left  = -FAN_COL_SPACING / 2.0;
    double standalone_x = col_x_left - FAN_SHELF_EXTRA - FAN_COL_DIAM * 2.0 - col_r;

    // Base plate extends from standalone cylinder to right shelf edge
    double base_left  = standalone_x - FAN_COL_DIAM;
    double base_right = FAN_COL_SPACING / 2.0 + FAN_SHELF_EXTRA + FAN_COL_DIAM / 2.0;
    double base_w = base_right - base_left;
    // Base plate as extruded trapezoid (chamfered edges).
    // Bottom face is full size, top face is inset by chamfer amount on all sides.
    {
        double ch = FAN_BASE_H;  // chamfer = base height (45°)
        double bx0 = base_left, bx1 = base_right;
        double by0 = -FAN_BASE_D / 2.0, by1 = FAN_BASE_D / 2.0;

        // Bottom face (z=0): full size
        // Top face (z=FAN_BASE_H): inset by ch on each side
        indexed_triangle_set base;
        // 8 vertices: 4 bottom, 4 top (inset)
        base.vertices = {
            Vec3f(float(bx0),      float(by0),      0.f),  // 0: bot-left-front
            Vec3f(float(bx1),      float(by0),      0.f),  // 1: bot-right-front
            Vec3f(float(bx1),      float(by1),      0.f),  // 2: bot-right-back
            Vec3f(float(bx0),      float(by1),      0.f),  // 3: bot-left-back
            Vec3f(float(bx0 + ch), float(by0 + ch), float(ch)),  // 4: top-left-front
            Vec3f(float(bx1 - ch), float(by0 + ch), float(ch)),  // 5: top-right-front
            Vec3f(float(bx1 - ch), float(by1 - ch), float(ch)),  // 6: top-right-back
            Vec3f(float(bx0 + ch), float(by1 - ch), float(ch)),  // 7: top-left-back
        };
        base.indices = {
            // Bottom face
            {0, 2, 1}, {0, 3, 2},
            // Top face
            {4, 5, 6}, {4, 6, 7},
            // Front chamfer (0,1 → 4,5)
            {0, 1, 5}, {0, 5, 4},
            // Right chamfer (1,2 → 5,6)
            {1, 2, 6}, {1, 6, 5},
            // Back chamfer (2,3 → 6,7)
            {2, 3, 7}, {2, 7, 6},
            // Left chamfer (3,0 → 7,4)
            {3, 0, 4}, {3, 4, 7},
        };
        its_merge(tower, base);
    }

    // Two vertical columns (cylinders)
    double col_x_right =  FAN_COL_SPACING / 2.0;
    {
        auto col_l = its_make_cylinder(col_r, total_height, 2.0 * M_PI / 32.0);
        its_translate(col_l, Vec3f(float(col_x_left), 0.f, 0.f));
        its_merge(tower, col_l);

        auto col_r_mesh = its_make_cylinder(col_r, total_height, 2.0 * M_PI / 32.0);
        its_translate(col_r_mesh, Vec3f(float(col_x_right), 0.f, 0.f));
        its_merge(tower, col_r_mesh);
    }

    // Standalone thin cylinder next to the model.
    // Tests stringing and fine cylinder cooling.
    {
        auto standalone = its_make_cylinder(col_r, total_height, 2.0 * M_PI / 32.0);
        its_translate(standalone, Vec3f(float(standalone_x), 0.f, 0.f));
        its_merge(tower, standalone);
    }

    // Overhang shelves at each level — bridge between columns with overhang
    // extending past columns on both sides
    double shelf_w = FAN_COL_SPACING + 2.0 * FAN_SHELF_EXTRA;
    for (int i = 0; i < num_levels; ++i) {
        double level_z = FAN_BASE_H + i * FAN_TOWER_LEVEL_HEIGHT;
        // Shelf at top of each level
        double shelf_z = level_z + FAN_TOWER_LEVEL_HEIGHT - FAN_SHELF_THICK;

        auto shelf = its_make_cube(shelf_w, FAN_SHELF_DEPTH, FAN_SHELF_THICK);
        its_translate(shelf, Vec3f(
            float(-shelf_w / 2.0),
            float(-FAN_SHELF_DEPTH / 2.0),
            float(shelf_z)));
        its_merge(tower, shelf);

        // Cone and wedge on all levels except the base (i=0)
        if (i > 0)
        {
        // Cone in the center of each level (tests fine detail cooling)
        {
            double cone_r = 2.0;   // base radius
            double cone_h = FAN_TOWER_LEVEL_HEIGHT - FAN_SHELF_THICK - 2.0;
            auto cone = its_make_cone(cone_r, cone_h, 2.0 * M_PI / 32.0);
            its_translate(cone, Vec3f(0.f, 0.f, float(level_z)));
            its_merge(tower, cone);
        }

        // Overhang wedge: extends from left column toward standalone cylinder.
        // Flat face on top, sloped face underneath (tests overhang cooling).
        // Overhang wedge: tall side at left column, vertex at standalone cylinder.
        // its_make_wedge: right-angle at origin, base along +X, height along +Z, depth in +Y.
        {
            double gap = (col_x_left - col_r) - (standalone_x + col_r); // distance between surfaces
            double wedge_base = gap + 2.0;  // +2mm overlap (1mm into each cylinder)
            double wedge_height = gap * std::tan(30.0 * M_PI / 180.0);
            auto wedge = its_make_wedge(wedge_base, wedge_height, FAN_COL_DIAM);

            // As built: right-angle at origin (0,0,0), base goes +X, height goes +Z.
            // The hypotenuse slopes from (base_x, 0, 0) to (0, 0, height).
            // This means: tall side (height) is at X=0, vertex (point) is at X=base.
            // We want: tall side at the RIGHT (at left column), vertex at LEFT (at cylinder).
            // So X=0 (tall side) should map to col_x_left, X=base (point) to standalone_x.
            // This means we need to mirror X and position so right edge is at column.

            // its_make_wedge: right-angle at (0,0,0), base along +X, height along +Z.
            // Triangle: (0,0,0)→(base,0,0)→(0,0,height). Hypotenuse slopes up-left.
            //
            // We want: short side (Z=height) on RIGHT at main column, vertex (Z=0) on
            // LEFT at standalone cylinder. Hypotenuse slopes up from right to left.
            // = the wedge as-is, just translated so X=0 is at the column.
            // But X=0 has the tall side and X=base has the point — that's backwards.
            // Mirror X so point is at X=0 (left) and tall side at X=base (right).
            for (auto& v : wedge.vertices)
                v.x() = float(wedge_base) - v.x();
            for (auto& f : wedge.indices)
                std::swap(f[0], f[1]);
            // Now: point at X=0 (left, Z=0), tall side at X=base (right, Z=height).
            // Hypotenuse slopes upward from left to right. We want it sloping
            // upward from RIGHT to LEFT. Flip Z.
            for (auto& v : wedge.vertices)
                v.z() = float(wedge_height) - v.z();
            for (auto& f : wedge.indices)
                std::swap(f[0], f[1]);

            // Position: X=0 (tall side) at left column, extending left toward cylinder
            // Shift left by wedge_base so the tall side (X=0) ends up at the right edge
            its_translate(wedge, Vec3f(
                float(col_x_left - col_r + 1.0 - wedge_base),  // right edge 1mm into column
                float(-FAN_COL_DIAM / 2.0),
                float(level_z)));
            its_merge(tower, wedge);
        }
        } // end if (i > 0)

        // Fan speed label on the shelf top face, shifted left
        {
            int fan_pct = (num_levels > 1) ? (i * 100) / (num_levels - 1) : 0;
            std::string label = std::to_string(fan_pct) + "%";

            auto text = make_block_text(label, FAN_LABEL_SIZE, 0.5, false);
            if (!text.empty()) {
                // Lay text flat on shelf top
                for (auto& v : text.vertices) {
                    float old_y = v.y();
                    float old_z = v.z();
                    v.y() = old_z;
                    v.z() = old_y;
                }

                // Shift left (toward -X column)
                its_translate(text, Vec3f(
                    float(-shelf_w / 4.0),
                    0.f,
                    float(shelf_z + FAN_SHELF_THICK)));
                its_merge(tower, text);
            }
        }
    }

    return tower;
}

// ---------------------------------------------------------------------------
// Shrinkage Gauge
// ---------------------------------------------------------------------------

static constexpr double HOLE_SIZE       = 5.0;   // mm — square through-hole size (fits caliper jaws)
static constexpr double HOLE_INTERVAL   = 25.0;  // mm — spacing between holes
static constexpr double SHRINK_LABEL_H  = 4.0;   // mm — font height for distance labels
static constexpr double SHRINK_LABEL_D  = 0.8;   // mm — label protrusion from surface
static constexpr double SHRINK_LABEL_PX = SHRINK_LABEL_H / 9.0; // pixel size for block text

indexed_triangle_set make_shrinkage_gauge(double length, int *skipped_holes)
{
    int skipped = 0;   // #40: count holes a boolean cut couldn't produce
    // Three bars joined at origin corner.
    // X-arm: (0,0,0) to (length, BAR_SECTION, BAR_SECTION)
    // Y-arm: (0,0,0) to (BAR_SECTION, length, BAR_SECTION)
    // Z-arm: (0,0,0) to (BAR_SECTION, BAR_SECTION, length)
    auto x_arm = its_make_cube(length, BAR_SECTION, BAR_SECTION);
    auto y_arm = its_make_cube(BAR_SECTION, length, BAR_SECTION);
    auto z_arm = its_make_cube(BAR_SECTION, BAR_SECTION, length);

    // #40: union the three arms into ONE clean manifold. The old code concatenated
    // them with its_merge, which left the shared corner cube self-intersecting
    // (three overlapping boxes). That self-intersection is what eventually made a
    // hole subtraction throw (corefine runs with throw_on_self_intersection), so
    // holes past ~125mm silently vanished at long arm lengths. A boolean union
    // resolves the corner, giving the hole subtraction below a clean solid to work
    // on.
    indexed_triangle_set gauge = x_arm;
    MeshBoolean::cgal::plus(gauge, y_arm);
    MeshBoolean::cgal::plus(gauge, z_arm);

    // #40: collect EVERY through-hole into one mesh and subtract it in a SINGLE
    // boolean op, instead of cutting one hole at a time. Cutting sequentially fed
    // the accumulating result back into CGAL each time; corefine's output grew
    // self-intersections that made the 6th+ cut throw with
    // throw_on_self_intersection — silently dropping the holes past ~125mm at long
    // arm lengths (and the per-cut float32 round-trip made it worse). The holes are
    // pairwise disjoint (>=25mm apart, each confined near its own arm axis), so
    // their union is a plain concatenation with no self-intersection, and one
    // corefine has nothing to accumulate.
    indexed_triangle_set holes;
    auto add_hole = [&](double hx, double hy, double hz,
                        double hw, double hh, double hd) {
        auto hole = its_make_cube(hw, hh, hd);
        its_translate(hole, Vec3f(float(hx), float(hy), float(hz)));
        its_merge(holes, hole);
    };

    // Number labels are plain geometry merges (not boolean cuts), so they're
    // collected during the hole passes and applied AFTER the subtraction.
    struct GaugeLabel { std::string text; double cx, cy, cz; int face; };
    std::vector<GaugeLabel> pending_labels;

    // Helper: add a raised number label on a face. make_block_text() returns the
    // digits standing UPRIGHT in the XZ plane (glyph horizontal = +X, glyph vertical
    // = +Z) protruding +Y — that's how the temperature tower reads them on a vertical
    // side face. Reorient onto the requested face, then translate the centre.
    auto add_label = [](indexed_triangle_set& mesh,
                        const std::string& text,
                        double cx, double cy, double cz,
                        int face) {
        // face: 0 = lay flat on a top (+Z) face (read from above);
        //       3 = stand on a +Y side face (read from the front).
        // BOTH are raised text viewed from the face's OUTWARD normal, where world
        // +X maps to the viewer's LEFT — so the glyphs (built left-to-right in +X)
        // read mirrored unless pre-flipped. make_block_text's mirror_x does that
        // flip. (Previously only face 0 was mirrored, so the Z-arm's face-3 labels
        // printed mirrored.)
        const bool mirror = (face == 0 || face == 3);
        auto label = make_block_text(text, SHRINK_LABEL_H, SHRINK_LABEL_D, mirror);
        if (label.empty()) return;

        if (face == 0) {
            // Tip the upright text down flat so it lies in XY and protrudes +Z.
            Transform3d rot = Transform3d::Identity();
            rot.rotate(Eigen::AngleAxisd(M_PI / 2.0, Vec3d::UnitX()));
            its_transform(label, rot);
        }
        // face 3 (+Y side): make_block_text already stands in XZ protruding +Y.
        its_translate(label, Vec3f(float(cx), float(cy), float(cz)));
        its_merge(mesh, label);
    };

    // Each hole's corner-side (NEAR) edge is placed exactly at the labeled
    // distance from the shared corner datum — that is the edge a caliper jaw
    // registers when measuring outward from the corner, so a hole labeled "25"
    // reads 25 mm. (Previously the hole was centered on the label, leaving the
    // near edge HOLE_SIZE/2 short, so the "25" hole measured 22.5 mm.) The loop
    // bound keeps the whole hole [pos, pos + HOLE_SIZE] inside the arm with a
    // small wall before the tip, regardless of the user-chosen arm length.

    // X-arm: through-holes go through Y (side, front to back); labels on the top
    // face (+Z), centered over each hole.
    for (double x = HOLE_INTERVAL; x + HOLE_SIZE <= length - 0.5; x += HOLE_INTERVAL) {
        int dist = int(x);
        add_hole(x, -0.01, (BAR_SECTION - HOLE_SIZE) / 2.0,
                 HOLE_SIZE, BAR_SECTION + 0.02, HOLE_SIZE);
        pending_labels.push_back({ std::to_string(dist),
                                   x + HOLE_SIZE / 2.0, BAR_SECTION / 2.0, BAR_SECTION, 0 });
    }

    // Y-arm: through-holes go through X (side, left to right); labels on the top
    // face (+Z), centered over each hole.
    for (double y = HOLE_INTERVAL; y + HOLE_SIZE <= length - 0.5; y += HOLE_INTERVAL) {
        int dist = int(y);
        add_hole(-0.01, y, (BAR_SECTION - HOLE_SIZE) / 2.0,
                 BAR_SECTION + 0.02, HOLE_SIZE, HOLE_SIZE);
        pending_labels.push_back({ std::to_string(dist),
                                   BAR_SECTION / 2.0, y + HOLE_SIZE / 2.0, BAR_SECTION, 0 });
    }

    // Z-arm (vertical): through-holes go through X (horizontal, left to right);
    // labels stand on the +Y side face, centered over each hole.
    for (double z = HOLE_INTERVAL; z + HOLE_SIZE <= length - 0.5; z += HOLE_INTERVAL) {
        int dist = int(z);
        add_hole(-0.01, (BAR_SECTION - HOLE_SIZE) / 2.0, z,
                 BAR_SECTION + 0.02, HOLE_SIZE, HOLE_SIZE);
        pending_labels.push_back({ std::to_string(dist),
                                   BAR_SECTION / 2.0, BAR_SECTION, z + HOLE_SIZE / 2.0, 3 });
    }

    // Subtract every hole in ONE boolean op (see the note above). All-or-nothing:
    // if this single corefine fails, the gauge keeps its arms but loses the marks,
    // which the skipped count surfaces to the user.
    if (!holes.indices.empty()) {
        try {
            MeshBoolean::cgal::minus(gauge, holes);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "Shrinkage gauge: hole subtraction failed";
            skipped = int(pending_labels.size());
        }
    }

    // Labels are plain geometry merges on top of the finished (cut) gauge.
    for (const GaugeLabel& lbl : pending_labels)
        add_label(gauge, lbl.text, lbl.cx, lbl.cy, lbl.cz, lbl.face);

    // Center at XY origin for bed placement
    its_translate(gauge, Vec3f(float(-length / 2.0), float(-length / 2.0), 0.f));

    if (skipped_holes)
        *skipped_holes = skipped;
    if (skipped > 0)
        BOOST_LOG_TRIVIAL(warning) << "Shrinkage gauge: " << skipped
                                   << " hole(s) could not be cut (length=" << length << ")";
    return gauge;
}

// ---------------------------------------------------------------------------
// Rounded-rectangle pad for YOLO flow calibration
// ---------------------------------------------------------------------------

indexed_triangle_set make_rounded_rect_pad(double width, double depth, double height,
                                           double corner_r, int segments)
{
    // Clamp radius
    double max_r = std::min(width, depth) / 2.0 - 0.01;
    if (corner_r > max_r) corner_r = max_r;
    if (corner_r < 0.0)   corner_r = 0.0;

    // Build 2D outline: 4 straight edges connected by 4 quarter-circle arcs
    // Outline goes counter-clockwise starting from bottom-right straight edge
    std::vector<Vec2f> outline;
    auto add_arc = [&](double cx, double cy, double start_angle, int n, double r) {
        for (int i = 0; i <= n; ++i) {
            double a = start_angle + i * (M_PI / 2.0) / n;
            outline.push_back(Vec2f(float(cx + r * cos(a)), float(cy + r * sin(a))));
        }
    };

    double r = corner_r;
    // Bottom-right corner (center at width-r, r)
    add_arc(width - r, r, -M_PI / 2.0, segments, r);
    // Top-right corner (center at width-r, depth-r)
    add_arc(width - r, depth - r, 0.0, segments, r);
    // Top-left corner (center at r, depth-r)
    add_arc(r, depth - r, M_PI / 2.0, segments, r);
    // Bottom-left corner (center at r, r)
    add_arc(r, r, M_PI, segments, r);

    int n = (int)outline.size();
    float h = float(height);

    indexed_triangle_set its;
    its.vertices.reserve(2 * n + 2);
    its.indices.reserve(4 * n);

    // Bottom and top ring vertices
    for (int i = 0; i < n; ++i)
        its.vertices.push_back(Vec3f(outline[i].x(), outline[i].y(), 0.f));
    for (int i = 0; i < n; ++i)
        its.vertices.push_back(Vec3f(outline[i].x(), outline[i].y(), h));

    // Side quads (2 triangles each)
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        int bi = i, bj = j, ti = i + n, tj = j + n;
        its.indices.push_back({bi, bj, tj});
        its.indices.push_back({bi, tj, ti});
    }

    // Bottom face (fan from vertex 0)
    for (int i = 1; i < n - 1; ++i)
        its.indices.push_back({0, i + 1, i});

    // Top face (fan from vertex n)
    for (int i = 1; i < n - 1; ++i)
        its.indices.push_back({n, n + i, n + i + 1});

    return its;
}

} // namespace Slic3r
