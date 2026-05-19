#include "TextureSkin.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Feature/SurfaceTexture/PerturbPolyline.hpp"

using namespace Slic3r;

namespace Slic3r::Feature::TextureSkin {

double sample_displacement_mm(const Vec3d &pos_mm, const Vec3d &normal, const Params &params)
{
    if (!params.image || params.image->empty()) return 0.0;

    const UVResult uvs = compute_uv(pos_mm, normal, params.mode, params.uv, params.bounds);
    double grey = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < uvs.count; ++i) {
        const UVSample &s = uvs.samples[i];
        grey += s.w * sample_bilinear(*params.image, s.u, s.v);
        wsum += s.w;
    }
    if (wsum > 0.0) grey /= wsum;

    // Centre at 0.5 and scale to ±amplitude.
    return (grey - 0.5) * 2.0 * params.amplitude_mm;
}

namespace ST = Slic3r::Feature::SurfaceTexture;

// Texture-skin offset: sample the grayscale image at the point's 3D
// position via UV projection, centre at 0.5, scale to ±amplitude.
static ST::OffsetProvider make_texture_provider(const Params &params)
{
    return [&params](const Vec2d &pos_scaled, const Vec2d &n2d) -> double {
        const Vec3d pos_mm(unscale<double>(static_cast<coord_t>(pos_scaled.x())),
                           unscale<double>(static_cast<coord_t>(pos_scaled.y())),
                           params.layer_z_mm);
        const Vec3d normal3d(n2d.x(), n2d.y(), 0.0);
        const double disp_mm = sample_displacement_mm(pos_mm, normal3d, params);
        return scaled<double>(disp_mm);
    };
}

static ST::PerturbSpacing make_texture_spacing(const Params &params)
{
    ST::PerturbSpacing s;
    s.point_distance_scaled = scaled<double>(std::max(params.point_dist_mm, 0.01));
    s.jitter_fraction       = 0.0; // deterministic spacing
    return s;
}

void texturize_polyline(Points &poly, const bool closed, const Params &params)
{
    if (!params.image || params.image->empty() || poly.size() < 2) return;
    ST::perturb_polyline(poly, closed, make_texture_spacing(params), make_texture_provider(params));
}

void texturize_polygon(Polygon &polygon, const Params &params)
{
    texturize_polyline(polygon.points, true, params);
}

void texturize_extrusion_line(Arachne::ExtrusionLine &ext_lines, const Params &params)
{
    if (!params.image || params.image->empty() || ext_lines.junctions.size() < 2) return;
    ST::perturb_extrusion_line(ext_lines, make_texture_spacing(params), make_texture_provider(params));
}

bool should_texturize(const PrintRegionConfig &config, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    const TextureSkinType t = config.texture_skin.value;
    if (t == TextureSkinType::None || layer_idx == 0) return false;

    const bool do_contours = (perimeter_idx == 0);
    const bool do_holes    = do_contours && (t == TextureSkinType::All);
    return is_contour ? do_contours : do_holes;
}

bool params_from_config(const PrintRegionConfig &config,
                        const BoundingBoxf3 &volume_bounds,
                        double layer_z_mm,
                        Params &out)
{
    if (config.texture_skin.value == TextureSkinType::None) return false;

    const Pattern pat = static_cast<Pattern>(config.texture_skin_pattern.value);
    const GrayImage *img = nullptr;
    if (pat == Pattern::Custom) {
        const std::string &path = config.texture_skin_custom_image.value;
        if (!path.empty()) img = &get_custom_image(path);
    } else {
        img = &get_pattern_image(pat, Slic3r::resources_dir());
    }
    if (!img || img->empty()) return false;

    out.image         = img;
    out.mode          = static_cast<UVMode>(config.texture_skin_uv_mode.value);
    out.uv.scale_u    = std::max(config.texture_skin_uv_scale.value, 1e-4);
    out.uv.scale_v    = out.uv.scale_u;
    // Aspect correction so non-square textures tile proportionally.
    const double tmax = std::max(double(img->width), double(img->height));
    out.uv.texture_aspect_u = (img->width  > 0) ? tmax / double(img->width)  : 1.0;
    out.uv.texture_aspect_v = (img->height > 0) ? tmax / double(img->height) : 1.0;
    out.uv.offset_u   = config.texture_skin_uv_offset_u.value;
    out.uv.offset_v   = config.texture_skin_uv_offset_v.value;
    out.uv.rotation_deg = config.texture_skin_uv_rotation.value;
    out.uv.mapping_blend = config.texture_skin_mapping_blend.value;
    out.amplitude_mm  = config.texture_skin_amplitude.value;
    out.point_dist_mm = config.texture_skin_point_dist.value;
    out.bounds        = volume_bounds;
    out.layer_z_mm    = layer_z_mm;
    return true;
}

Polygon apply_texture_skin(const Polygon &polygon,
                           const PrintRegionConfig &base_config,
                           const PerimeterRegions &perimeter_regions,
                           const size_t layer_idx, const size_t perimeter_idx, const bool is_contour,
                           const BoundingBoxf3 &volume_bounds,
                           double layer_z_mm)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    auto apply_on = [&](const Polygon &poly, const PrintRegionConfig &cfg) -> Polygon {
        if (!should_texturize(cfg, layer_idx, perimeter_idx, is_contour)) return poly;
        Params params;
        if (!params_from_config(cfg, volume_bounds, layer_z_mm, params)) return poly;
        Polygon out = poly;
        texturize_polygon(out, params);
        return out;
    };

    if (perimeter_regions.empty()) return apply_on(polygon, base_config);

    PolylineRegionSegments segments = polygon_segmentation(polygon, base_config, perimeter_regions);
    if (segments.size() == 1) return apply_on(polygon, segments.front().config);

    Polygon merged;
    for (PolylineRegionSegment &seg : segments) {
        const PrintRegionConfig &cfg = seg.config;
        if (should_texturize(cfg, layer_idx, perimeter_idx, is_contour)) {
            Params params;
            if (params_from_config(cfg, volume_bounds, layer_z_mm, params)) {
                texturize_polyline(seg.polyline.points, false, params);
            }
        }
        if (seg.polyline.empty()) continue;
        if (!merged.empty() && merged.back() == seg.polyline.front()) merged.points.pop_back();
        Slic3r::append(merged.points, std::move(seg.polyline.points));
    }
    if (!merged.empty() && merged.front() == merged.back()) merged.points.pop_back();
    return merged.empty() ? polygon : merged;
}

Arachne::ExtrusionLine apply_texture_skin(const Arachne::ExtrusionLine &extrusion,
                                          const PrintRegionConfig &base_config,
                                          const PerimeterRegions &perimeter_regions,
                                          const size_t layer_idx, const size_t perimeter_idx, const bool is_contour,
                                          const BoundingBoxf3 &volume_bounds,
                                          double layer_z_mm)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    if (perimeter_regions.empty()) {
        if (!should_texturize(base_config, layer_idx, perimeter_idx, is_contour)) return extrusion;
        Params params;
        if (!params_from_config(base_config, volume_bounds, layer_z_mm, params)) return extrusion;
        ExtrusionLine out = extrusion;
        texturize_extrusion_line(out, params);
        return out;
    }

    ExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine out(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);
    for (ExtrusionRegionSegment &seg : segments) {
        const PrintRegionConfig &cfg = seg.config;
        if (should_texturize(cfg, layer_idx, perimeter_idx, is_contour)) {
            Params params;
            if (params_from_config(cfg, volume_bounds, layer_z_mm, params)) {
                texturize_extrusion_line(seg.extrusion, params);
            }
        }
        if (seg.extrusion.empty()) continue;
        if (!out.empty() && out.back().p == seg.extrusion.front().p) out.junctions.pop_back();
        Slic3r::append(out.junctions, std::move(seg.extrusion.junctions));
    }
    return out.empty() ? extrusion : out;
}

} // namespace Slic3r::Feature::TextureSkin
