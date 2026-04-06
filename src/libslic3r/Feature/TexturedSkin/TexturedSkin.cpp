///|/ Copyright (c) Prusa Research 2026
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TexturedSkin.hpp"

#include "libslic3r/Point.hpp"
#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/libslic3r.h"

#include <nanosvg/nanosvg.h>
#include <boost/log/trivial.hpp>
#include <cmath>
#include <algorithm>

namespace Slic3r::Feature::TexturedSkin {

// --- PatternSampler ---

std::shared_ptr<PatternSampler> PatternSampler::create(const std::string &svg_path, double tile_size_mm, double tile_height_mm, double resolution)
{
    if (svg_path.empty() || tile_size_mm <= 0 || resolution <= 0)
        return nullptr;

    NSVGimage_ptr image = nsvgParseFromFile(svg_path, "mm", 96.0f);
    if (!image || image->width <= 0 || image->height <= 0) {
        BOOST_LOG_TRIVIAL(warning) << "TexturedSkin: Failed to load SVG file: " << svg_path;
        return nullptr;
    }

    auto sampler = std::shared_ptr<PatternSampler>(new PatternSampler());

    double scale = tile_size_mm / image->width;
    sampler->m_tile_w = tile_size_mm;
    sampler->m_tile_h = (tile_height_mm > 0) ? tile_height_mm : image->height * scale;
    sampler->m_resolution = resolution;
    sampler->m_grid_w = std::max(1, static_cast<int>(std::ceil(sampler->m_tile_w * resolution)));
    sampler->m_grid_h = std::max(1, static_cast<int>(std::ceil(sampler->m_tile_h * resolution)));

    // Tessellate SVG paths into polygons.
    constexpr double tess_tol_mm = 0.1;
    double tess_tol = (tess_tol_mm * tess_tol_mm) / SCALING_FACTOR / SCALING_FACTOR;
    NSVGLineParams params{tess_tol};
    ExPolygonsWithIds shapes = create_shape_with_ids(*image, params);

    // Extract brightness per result entry, matching the iteration order
    // of create_shape_with_ids: for each visible NSVGshape, it produces
    // one entry for fill (if used) and one for stroke (if used).
    // NanoSVG color: 0xAABBGGRR. ITU-R BT.601 perceived brightness.
    // black → 1.0 displacement, white → 0.0, gray → proportional.
    auto color_to_displacement = [](unsigned int color) -> float {
        uint8_t r = color & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = (color >> 16) & 0xFF;
        float brightness = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
        return 1.0f - brightness; // invert: black=max, white=none
    };

    // Build displacement values matching create_shape_with_ids output order.
    std::vector<float> entry_displacement;
    for (const NSVGshape *ns = image->shapes; ns; ns = ns->next) {
        if (!(ns->flags & NSVG_FLAGS_VISIBLE)) continue;
        bool has_fill = ns->fill.type != NSVG_PAINT_NONE;
        bool has_stroke = ns->stroke.type != NSVG_PAINT_NONE && ns->strokeWidth > 1e-5f;
        if (!has_fill && !has_stroke) continue;
        if (has_fill) {
            float d = (ns->fill.type == NSVG_PAINT_COLOR) ? color_to_displacement(ns->fill.color) : 1.0f;
            entry_displacement.push_back(d);
        }
        if (has_stroke) {
            float d = (ns->stroke.type == NSVG_PAINT_COLOR) ? color_to_displacement(ns->stroke.color) : 1.0f;
            entry_displacement.push_back(d);
        }
    }

    // Match ExPolygonsWithIds entries to displacement values.
    struct PolyBright { const ExPolygon *poly; float displacement; };
    std::vector<PolyBright> poly_list;
    for (size_t i = 0; i < shapes.size(); ++i) {
        float disp = (i < entry_displacement.size()) ? entry_displacement[i] : 1.0f;
        for (const auto &ep : shapes[i].expoly)
            poly_list.push_back({&ep, disp});
    }

    if (poly_list.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "TexturedSkin: SVG contains no usable shapes: " << svg_path;
        return nullptr;
    }

    // Pre-compute per-polygon bounding boxes for fast rejection during rasterization.
    struct PolyWithBB {
        const ExPolygon *poly;
        float displacement;
        BoundingBox bb;
    };
    std::vector<PolyWithBB> poly_bb_list;
    poly_bb_list.reserve(poly_list.size());
    BoundingBox total_bb;
    for (const auto &pb : poly_list) {
        BoundingBox bb = get_extents(*pb.poly);
        total_bb.merge(bb);
        poly_bb_list.push_back({pb.poly, pb.displacement, bb});
    }
    double poly_w = static_cast<double>(total_bb.size().x());
    double poly_h = static_cast<double>(total_bb.size().y());

    // Rasterize into grayscale heightmap grid.
    // For each grid cell, test only polygons whose bounding box contains the point.
    // This reduces O(cells × polygons) to roughly O(cells × overlapping_polygons).
    sampler->m_grid.resize(sampler->m_grid_w * sampler->m_grid_h, 0.0f);
    for (int gy = 0; gy < sampler->m_grid_h; ++gy) {
        for (int gx = 0; gx < sampler->m_grid_w; ++gx) {
            double fx = (gx + 0.5) / sampler->m_grid_w;
            double fy = (gy + 0.5) / sampler->m_grid_h;
            Point pt(total_bb.min.x() + static_cast<coord_t>(fx * poly_w),
                     total_bb.min.y() + static_cast<coord_t>(fy * poly_h));
            // Iterate in REVERSE (painter's order). Skip polygons whose
            // bounding box doesn't contain the test point.
            for (auto it = poly_bb_list.rbegin(); it != poly_bb_list.rend(); ++it) {
                if (!it->bb.contains(pt))
                    continue; // fast rejection — skips ~95% of polygons
                if (it->poly->contains(pt)) {
                    sampler->m_grid[gy * sampler->m_grid_w + gx] = it->displacement;
                    break;
                }
            }
        }
    }

    int filled = 0;
    for (float c : sampler->m_grid)
        if (c > 0.01f) filled++;

    BOOST_LOG_TRIVIAL(debug) << "TexturedSkin: loaded " << svg_path
        << " grid " << sampler->m_grid_w << "x" << sampler->m_grid_h
        << " filled " << filled << "/" << sampler->m_grid.size();

    return sampler;
}

double PatternSampler::sample(double u, double v) const
{
    if (m_grid.empty() || m_tile_w <= 0 || m_tile_h <= 0)
        return 0.0;

    double u_mod = std::fmod(u, m_tile_w);
    if (u_mod < 0) u_mod += m_tile_w;
    double v_mod = std::fmod(v, m_tile_h);
    if (v_mod < 0) v_mod += m_tile_h;

    // Bilinear interpolation for smooth transitions.
    double fx = u_mod * m_resolution - 0.5;
    double fy = v_mod * m_resolution - 0.5;
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    double dx = fx - x0;
    double dy = fy - y0;

    auto gv = [&](int gx, int gy) -> double {
        gx = ((gx % m_grid_w) + m_grid_w) % m_grid_w;
        gy = ((gy % m_grid_h) + m_grid_h) % m_grid_h;
        return static_cast<double>(m_grid[gy * m_grid_w + gx]);
    };

    return gv(x0, y0)     * (1-dx) * (1-dy) +
           gv(x0+1, y0)   * dx     * (1-dy) +
           gv(x0, y0+1)   * (1-dx) * dy     +
           gv(x0+1, y0+1) * dx     * dy;
}

// --- Helpers ---

namespace {

struct PerimeterInfo {
    Vec2d  centroid;
    double perimeter_mm;
    double effective_radius;
};

PerimeterInfo compute_perimeter_info(const Polygon &polygon)
{
    PerimeterInfo info;
    info.centroid = polygon.centroid().cast<double>();
    double perim = 0;
    const Point *prev = &polygon.points.back();
    for (const Point &pt : polygon.points) {
        perim += (pt - *prev).cast<double>().norm();
        prev = &pt;
    }
    info.perimeter_mm = unscale<double>(static_cast<coord_t>(perim));
    info.effective_radius = info.perimeter_mm / (2.0 * M_PI);
    return info;
}

double find_seam_offset(const Polygon &polygon, const Vec2d &centroid)
{
    // Find the exact arc-length where the perimeter crosses angle=0 from centroid.
    // Interpolate between the two vertices that straddle angle=0 to avoid
    // snapping to the nearest vertex (which causes spiraling on spheres).
    double arc = 0;
    double prev_angle = 0;
    double prev_arc = 0;
    const Point *prev = &polygon.points.back();
    bool first = true;

    for (const Point &pt : polygon.points) {
        Vec2d p = pt.cast<double>();
        double a = std::atan2(p.y() - centroid.y(), p.x() - centroid.x());
        double seg_len = (pt - *prev).cast<double>().norm();

        if (!first) {
            // Check if angle=0 is crossed between prev_angle and a
            // (angle=0 means the +X direction from centroid)
            bool crosses = (prev_angle < 0 && a >= 0) || (prev_angle >= 0 && a < 0);
            // Avoid false crossings at ±π (the -X direction)
            if (crosses && std::abs(prev_angle) < 2.0 && std::abs(a) < 2.0) {
                // Interpolate: at what fraction between prev and current does angle=0 occur?
                double t = std::abs(prev_angle) / (std::abs(prev_angle) + std::abs(a));
                double crossing_arc = prev_arc + t * seg_len;
                return unscale<double>(static_cast<coord_t>(crossing_arc));
            }
        }

        prev_angle = a;
        prev_arc = arc;
        arc += seg_len;
        prev = &pt;
        first = false;
    }

    // Fallback: return 0 if no crossing found (shouldn't happen for closed polygons)
    return 0;
}

// (Mercator ref circumference removed — now uses tile_width directly)

double compute_angle(const Vec2d &pos_scaled, const PerimeterInfo &info)
{
    double angle = std::atan2(pos_scaled.y() - info.centroid.y(),
                              pos_scaled.x() - info.centroid.x());
    if (angle < 0) angle += 2.0 * M_PI;
    return angle;
}

inline double us(double v) { return unscale<double>(static_cast<coord_t>(v)); }

std::pair<double, double> compute_uv(
    const Vec2d &pos_scaled, MappingMode mode, double arc_length,
    double layer_z, const PerimeterInfo &info, double tile_width)
{
    switch (mode) {
    case MappingMode::PaintedOn:
        return {arc_length, layer_z};

    case MappingMode::Mercator: {
        // Conformal Mercator: u = angle × R, v scaled to preserve local aspect ratio.
        // R = tile_width / 2π (the user sets tile_width = circumference at equator).
        // For conformal mapping, vertical scale must match horizontal scale:
        //   horizontal scale at this layer = circumference / (2π)
        //   so v must accumulate at rate (tile_width / circumference) per mm of Z
        // This is a linear approximation that's exact for cylinders and close
        // for gentle curvature. True Mercator (ln-tan formula) requires knowing
        // the equator position which isn't available per-layer.
        double ref_radius = tile_width / (2.0 * M_PI);
        double u = compute_angle(pos_scaled, info) * ref_radius;
        // Scale v so vertical density matches horizontal density at this layer
        double v = layer_z * (tile_width / std::max(info.perimeter_mm, 1.0));
        return {u, v};
    }

    case MappingMode::StretchFit: {
        int n = std::max(1, static_cast<int>(std::round(info.perimeter_mm / tile_width)));
        return {arc_length * (tile_width / (info.perimeter_mm / n)), layer_z};
    }

    case MappingMode::StampFront:  return { us(pos_scaled.x()),  layer_z};
    case MappingMode::StampBack:   return {-us(pos_scaled.x()),  layer_z};
    case MappingMode::StampLeft:   return { us(pos_scaled.y()),  layer_z};
    case MappingMode::StampRight:  return {-us(pos_scaled.y()),  layer_z};
    case MappingMode::StampTop:    return { us(pos_scaled.x()),  us(pos_scaled.y())};
    case MappingMode::StampBottom: return { us(pos_scaled.x()), -us(pos_scaled.y())};

    case MappingMode::Adaptive: {
        // Blend between PaintedOn (arc-length) and Mercator (angle-based).
        // Large perimeters (many tiles fit) → PaintedOn is fine.
        // Small perimeters (few tiles) → Mercator keeps vertical alignment.
        double ref_radius = tile_width / (2.0 * M_PI);
        double u_merc = compute_angle(pos_scaled, info) * ref_radius;
        double blend = std::clamp((info.perimeter_mm / tile_width - 2.0) / 2.0, 0.0, 1.0);
        return {blend * arc_length + (1.0 - blend) * u_merc, layer_z};
    }
    }
    return {arc_length, layer_z};
}

} // anonymous namespace

// --- Main displacement function ---

void textured_polygon(
    Polygon              &polygon,
    const PatternSampler &sampler,
    double                layer_z,
    double                thickness,
    double                point_distance,
    MappingMode           mapping)
{
    if (polygon.points.size() < 3 || point_distance <= 0 || thickness <= 0)
        return;

    PerimeterInfo pinfo = compute_perimeter_info(polygon);

    if (pinfo.perimeter_mm < sampler.tile_width() * 0.5)
        return;

    double seam_offset = find_seam_offset(polygon, pinfo.centroid);

    Points out;
    out.reserve(polygon.points.size() * 2);

    double arc_length = -seam_offset;
    double dist_to_next = 0;
    double prev_displacement = 0;
    double prev_u = -1e9;
    // Rate limiter: allow displacement to change by 3× point_distance per step.
    // Too low = horizontal streaking (slow ramps). Too high = sharp spikes.
    // 3× allows full 0→max transition in ~3 sample points.
    double max_disp_change = scaled(point_distance * 3.0);

    Point *p0 = &polygon.points.back();
    for (Point &p1 : polygon.points) {
        Vec2d edge = (p1 - *p0).cast<double>();
        double edge_len = edge.norm();
        if (edge_len < 1.0) {
            p0 = &p1;
            continue;
        }

        Vec2d edge_dir = edge / edge_len;
        Vec2d outward_normal(edge_dir.y(), -edge_dir.x());

        double edge_len_mm = unscale<double>(static_cast<coord_t>(edge_len));
        double walked = dist_to_next;

        while (walked < edge_len_mm) {
            double t = walked / edge_len_mm;
            Vec2d pos = p0->cast<double>() + edge * t;

            auto [u, v] = compute_uv(pos, mapping, arc_length + walked, layer_z, pinfo, sampler.tile_width());

            if (std::abs(u - prev_u) > pinfo.perimeter_mm * 0.5)
                prev_displacement = 0;
            prev_u = u;

            // sample() now returns 0.0-1.0 grayscale, not binary.
            // Displacement is proportional to the sampled height.
            double pattern_val = sampler.sample(u, v);
            double target_disp = pattern_val * scaled(thickness);

            double displacement = std::clamp(target_disp,
                prev_displacement - max_disp_change,
                prev_displacement + max_disp_change);
            prev_displacement = displacement;

            Vec2d pt = pos + outward_normal * displacement;
            out.emplace_back(pt.cast<coord_t>());

            walked += point_distance;
        }

        dist_to_next = walked - edge_len_mm;
        arc_length += edge_len_mm;
        p0 = &p1;
    }

    if (out.size() >= 3)
        polygon.points = std::move(out);
}

} // namespace Slic3r::Feature::TexturedSkin
