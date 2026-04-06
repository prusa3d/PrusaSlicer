#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Feature/SurfaceTexture/PerturbPolyline.hpp"

#include "FuzzySkin.hpp"

using namespace Slic3r;
namespace ST = Slic3r::Feature::SurfaceTexture;

namespace Slic3r::Feature::FuzzySkin {

// Fuzzy-skin offset: uniform random in [−thickness, +thickness], independent
// of position. Matches the original fuzzy_skin behaviour.
static ST::OffsetProvider make_fuzzy_provider(double fuzzy_skin_thickness)
{
    return [fuzzy_skin_thickness](const Vec2d &, const Vec2d &) -> double {
        return ST::random_unit() * (fuzzy_skin_thickness * 2.0) - fuzzy_skin_thickness;
    };
}

static ST::PerturbSpacing make_fuzzy_spacing(double fuzzy_skin_point_distance)
{
    ST::PerturbSpacing s;
    s.point_distance_scaled = fuzzy_skin_point_distance;
    s.jitter_fraction       = 0.25; // step varies in 75%..125% of nominal
    return s;
}

void fuzzy_polyline(Points &poly, const bool closed, const double fuzzy_skin_thickness, const double fuzzy_skin_point_distance)
{
    ST::perturb_polyline(poly, closed,
                         make_fuzzy_spacing(fuzzy_skin_point_distance),
                         make_fuzzy_provider(fuzzy_skin_thickness));
}

void fuzzy_polygon(Polygon &polygon, double fuzzy_skin_thickness, double fuzzy_skin_point_distance)
{
    fuzzy_polyline(polygon.points, true, fuzzy_skin_thickness, fuzzy_skin_point_distance);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, const double fuzzy_skin_thickness, const double fuzzy_skin_point_distance)
{
    ST::perturb_extrusion_line(ext_lines,
                               make_fuzzy_spacing(fuzzy_skin_point_distance),
                               make_fuzzy_provider(fuzzy_skin_thickness));
}

bool should_fuzzify(const PrintRegionConfig &config, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    const FuzzySkinType fuzzy_skin_type = config.fuzzy_skin.value;

    if (fuzzy_skin_type == FuzzySkinType::None || layer_idx <= 0) {
        return false;
    }

    const bool fuzzify_contours = perimeter_idx == 0;
    const bool fuzzify_holes    = fuzzify_contours && fuzzy_skin_type == FuzzySkinType::All;

    return is_contour ? fuzzify_contours : fuzzify_holes;
}

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    auto apply_fuzzy_skin_on_polygon = [&layer_idx, &perimeter_idx, &is_contour](const Polygon &polygon, const PrintRegionConfig &config) -> Polygon {
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            Polygon fuzzified_polygon = polygon;
            fuzzy_polygon(fuzzified_polygon, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));

            return fuzzified_polygon;
        } else {
            return polygon;
        }
    };

    if (perimeter_regions.empty()) {
        return apply_fuzzy_skin_on_polygon(polygon, base_config);
    }

    PolylineRegionSegments segments = polygon_segmentation(polygon, base_config, perimeter_regions);
    if (segments.size() == 1) {
        const PrintRegionConfig &config = segments.front().config;
        return apply_fuzzy_skin_on_polygon(polygon, config);
    }

    Polygon fuzzified_polygon;
    for (PolylineRegionSegment &segment : segments) {
        const PrintRegionConfig &config = segment.config;
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            fuzzy_polyline(segment.polyline.points, false, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));
        }

        assert(!segment.polyline.empty());
        if (segment.polyline.empty()) {
            continue;
        } else if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == segment.polyline.front()) {
            // Remove the last point to avoid duplicate points.
            fuzzified_polygon.points.pop_back();
        }

        Slic3r::append(fuzzified_polygon.points, std::move(segment.polyline.points));
    }

    assert(!fuzzified_polygon.empty());
    if (fuzzified_polygon.front() == fuzzified_polygon.back()) {
        // Remove the last point to avoid duplicity between the first and the last point.
        fuzzified_polygon.points.pop_back();
    }

    return fuzzified_polygon;
}

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    if (perimeter_regions.empty()) {
        if (should_fuzzify(base_config, layer_idx, perimeter_idx, is_contour)) {
            ExtrusionLine fuzzified_extrusion = extrusion;
            fuzzy_extrusion_line(fuzzified_extrusion, scaled<double>(base_config.fuzzy_skin_thickness.value), scaled<double>(base_config.fuzzy_skin_point_dist.value));

            return fuzzified_extrusion;
        } else {
            return extrusion;
        }
    }

    ExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine           fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

    for (ExtrusionRegionSegment &segment : segments) {
        const PrintRegionConfig &config = segment.config;
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            fuzzy_extrusion_line(segment.extrusion, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));
        }

        assert(!segment.extrusion.empty());
        if (segment.extrusion.empty()) {
            continue;
        } else if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p) {
            // Remove the last point to avoid duplicate points (We don't care if the width of both points is different.).
            fuzzified_extrusion.junctions.pop_back();
        }

        Slic3r::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
    }

    assert(!fuzzified_extrusion.empty());

    return fuzzified_extrusion;
}

} // namespace Slic3r::Feature::FuzzySkin
