///|/ Copyright (c) Prusa Research 2026
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TexturedSkin.hpp"

#include "libslic3r/Point.hpp"
#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/libslic3r.h"

#include <nanosvg/nanosvg.h>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <algorithm>
#include <fstream>

namespace Slic3r::Feature::TexturedSkin {

// --- PatternSampler ---

std::shared_ptr<PatternSampler> PatternSampler::create(const std::string &svg_path, double tile_size_mm, double tile_height_mm, double resolution)
{
    if (svg_path.empty() || tile_size_mm <= 0 || resolution <= 0)
        return nullptr;

    // --- PNG path: load grayscale pixels directly into grid ---
    if (boost::algorithm::iends_with(svg_path, ".png")) {
        // Read file into memory
        std::ifstream file(svg_path, std::ios::binary | std::ios::ate);
        if (!file) {
            BOOST_LOG_TRIVIAL(warning) << "TexturedSkin: Failed to open PNG file: " << svg_path;
            return nullptr;
        }
        size_t file_size = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> file_data(file_size);
        file.read(reinterpret_cast<char*>(file_data.data()), file_size);

        png::ImageGreyscale img;
        png::ReadBuf rbuf{file_data.data(), file_data.size()};
        if (!png::decode_png(rbuf, img) || img.cols == 0 || img.rows == 0) {
            BOOST_LOG_TRIVIAL(warning) << "TexturedSkin: Failed to decode PNG: " << svg_path;
            return nullptr;
        }

        auto sampler = std::shared_ptr<PatternSampler>(new PatternSampler());
        sampler->m_tile_w = tile_size_mm;
        double aspect = static_cast<double>(img.rows) / img.cols;
        sampler->m_tile_h = (tile_height_mm > 0) ? tile_height_mm : tile_size_mm * aspect;
        sampler->m_resolution = resolution;

        // Downsample the PNG to match the target resolution.
        // 512×512 PNGs at tile_size=5mm give 102 px/mm, but the perimeter
        // is sampled at ~0.3mm intervals. Without downsampling, fine features
        // (woodgrain lines, weave threads) fall between sample points → aliasing.
        // Box-filter downsampling averages each block, capturing fine detail.
        int target_w = std::max(1, static_cast<int>(std::ceil(sampler->m_tile_w * resolution)));
        int target_h = std::max(1, static_cast<int>(std::ceil(sampler->m_tile_h * resolution)));
        sampler->m_grid_w = target_w;
        sampler->m_grid_h = target_h;
        sampler->m_grid.resize(target_w * target_h, 0.0f);

        // Find min/max for auto-normalization
        uint8_t px_min = 255, px_max = 0;
        for (uint8_t px : img.buf) {
            px_min = std::min(px_min, px);
            px_max = std::max(px_max, px);
        }
        float inv_range = (px_max > px_min) ? 1.0f / (px_max - px_min) : 1.0f;

        // Box-filter downsample: each grid cell averages a block of PNG pixels.
        for (int gy = 0; gy < target_h; ++gy) {
            int py0 = static_cast<int>(static_cast<double>(gy) / target_h * img.rows);
            int py1 = static_cast<int>(static_cast<double>(gy + 1) / target_h * img.rows);
            py1 = std::min(py1, static_cast<int>(img.rows));
            for (int gx = 0; gx < target_w; ++gx) {
                int px0 = static_cast<int>(static_cast<double>(gx) / target_w * img.cols);
                int px1 = static_cast<int>(static_cast<double>(gx + 1) / target_w * img.cols);
                px1 = std::min(px1, static_cast<int>(img.cols));
                // Average the pixel block
                float sum = 0;
                int count = 0;
                for (int py = py0; py < py1; ++py)
                    for (int px = px0; px < px1; ++px) {
                        sum += static_cast<float>(img.buf[py * img.cols + px] - px_min) * inv_range;
                        ++count;
                    }
                float avg = (count > 0) ? sum / count : 0.0f;
                // Standard heightmap: white = raised = max displacement
                sampler->m_grid[gy * target_w + gx] = avg;
            }
        }

        sampler->m_resolution = resolution;

        int filled = 0;
        for (float c : sampler->m_grid) if (c > 0.01f) filled++;
        BOOST_LOG_TRIVIAL(debug) << "TexturedSkin: loaded PNG " << svg_path
            << " " << img.cols << "x" << img.rows << " px"
            << " tile " << sampler->m_tile_w << "x" << sampler->m_tile_h << " mm"
            << " filled " << filled << "/" << sampler->m_grid.size();
        return sampler;
    }

    // --- SVG path ---
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

    // Auto-normalize displacement range so the lightest shape = 0 and darkest = 1.
    // Without this, an all-gray SVG (no black or white) would produce uniform
    // displacement with barely visible variation.
    if (!entry_displacement.empty()) {
        float min_d = *std::min_element(entry_displacement.begin(), entry_displacement.end());
        float max_d = *std::max_element(entry_displacement.begin(), entry_displacement.end());
        float range = max_d - min_d;
        if (range > 0.01f) {
            for (float &d : entry_displacement)
                d = (d - min_d) / range;
        }
        // If all shapes have the same brightness, they all become 1.0 (full displacement)
    }

    // Match ExPolygonsWithIds entries to displacement values.
    struct PolyBright { const ExPolygon *poly; float displacement; };
    std::vector<PolyBright> poly_list;
    for (size_t i = 0; i < shapes.size(); ++i) {
        float disp = (i < entry_displacement.size()) ? entry_displacement[i] : 0.0f;
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
    // Use separate X/Y resolutions: grid may not be square, and tile_w/tile_h
    // may have different ratios (e.g. PNG with explicit tile_height).
    double res_x = m_grid_w / m_tile_w;
    double res_y = m_grid_h / m_tile_h;
    double fx = u_mod * res_x - 0.5;
    double fy = v_mod * res_y - 0.5;
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

/// Convert scaled coordinate to mm.
inline double to_mm(double scaled_val) { return unscale<double>(static_cast<coord_t>(scaled_val)); }

/// Angle of a point from centroid, in [0, 2π).
double angle_from_centroid(const Vec2d &pos_scaled, const PerimeterInfo &info)
{
    double a = std::atan2(pos_scaled.y() - info.centroid.y(),
                          pos_scaled.x() - info.centroid.x());
    if (a < 0) a += 2.0 * M_PI;
    return a;
}

/// Compute (u, v) pattern coordinates for a perimeter point.
///
/// Each mode implements a different UV projection:
///
/// ArcLength:   u = distance along perimeter (mm), v = layer height (mm).
///              Pattern has consistent physical tile size everywhere.
///              The seam_offset ensures stable alignment across layers.
///
/// Cylindrical: u = angle around centroid × (tile_width / 2π), v = layer height.
///              The pattern wraps seamlessly around the circumference.
///              tile_width should equal the object's circumference for one wrap.
///              Angle-based u is inherently stable across layers (no seam drift).
///
/// Spherical:   u = longitude (angle) × (tile_width / 2π),
///              v = latitude mapped from circumference ratio.
///              Uses conformal v-scaling: layers with smaller circumference
///              get proportionally stretched v to preserve local shape.
///              tile_width should equal the equator circumference.
///
/// Planar:      u = X position (mm), v = Z position (mm).
///              Projects the pattern from the front (XZ plane).
///              Simple, predictable, works well on flat/boxy surfaces.
///
/// Triplanar:   Automatically selects the best planar projection per point
///              based on the outward normal direction at that point.
///              For points facing X: u=Y, v=Z. Facing Y: u=X, v=Z. Facing Z: u=X, v=Y.
///              Works on ANY shape without user configuration.
///              Inspired by triplanar mapping in game engines and PR #15335.
std::pair<double, double> compute_uv(
    const Vec2d &pos_scaled, const Vec2d &normal_dir, MappingMode mode,
    double arc_length, double layer_z, const PerimeterInfo &info, double tile_width)
{
    switch (mode) {
    case MappingMode::PaintedOn:
        return {arc_length, layer_z};

    case MappingMode::Mercator: {
        double ref_radius = tile_width / (2.0 * M_PI);
        double u = angle_from_centroid(pos_scaled, info) * ref_radius;
        double v = layer_z * (tile_width / std::max(info.perimeter_mm, 1.0));
        return {u, v};
    }

    case MappingMode::StretchFit: {
        int n = std::max(1, static_cast<int>(std::round(info.perimeter_mm / tile_width)));
        return {arc_length * (tile_width / (info.perimeter_mm / n)), layer_z};
    }

    case MappingMode::StampFront:  return { to_mm(pos_scaled.x()),  layer_z};
    case MappingMode::StampBack:   return {-to_mm(pos_scaled.x()),  layer_z};
    case MappingMode::StampLeft:   return { to_mm(pos_scaled.y()),  layer_z};
    case MappingMode::StampRight:  return {-to_mm(pos_scaled.y()),  layer_z};
    case MappingMode::StampTop:    return { to_mm(pos_scaled.x()),  to_mm(pos_scaled.y())};
    case MappingMode::StampBottom: return { to_mm(pos_scaled.x()), -to_mm(pos_scaled.y())};

    case MappingMode::Adaptive: {
        double ref_radius = tile_width / (2.0 * M_PI);
        double u_merc = angle_from_centroid(pos_scaled, info) * ref_radius;
        double blend = std::clamp((info.perimeter_mm / tile_width - 2.0) / 2.0, 0.0, 1.0);
        return {blend * arc_length + (1.0 - blend) * u_merc, layer_z};
    }

    case MappingMode::Cylindrical: {
        double u = angle_from_centroid(pos_scaled, info) * (tile_width / (2.0 * M_PI));
        return {u, layer_z};
    }

    case MappingMode::Triplanar: {
        double nx = std::abs(normal_dir.x());
        double ny = std::abs(normal_dir.y());
        if (nx >= ny)
            return {to_mm(pos_scaled.y()), layer_z};
        else
            return {to_mm(pos_scaled.x()), layer_z};
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
    MappingMode           mapping,
    bool                  invert)
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

            auto [u, v] = compute_uv(pos, outward_normal, mapping, arc_length + walked, layer_z, pinfo, sampler.tile_width());

            double val = sampler.sample(u, v);
            if (invert) val = 1.0 - val;
            double displacement = val * scaled(thickness);

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
