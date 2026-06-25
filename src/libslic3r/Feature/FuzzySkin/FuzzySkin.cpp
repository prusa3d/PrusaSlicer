#include <random>
#include <memory>
#include <cmath>

#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <libnoise/noise.h>

#include "FuzzySkin.hpp"

using namespace Slic3r;

namespace Slic3r::Feature::FuzzySkin {

// Produces a random value between 0 and 1. Thread-safe.
static double random_value()
{
    thread_local std::random_device rd;
    // Hash thread ID for random number seed if no hardware rng seed is available
    thread_local std::mt19937                           gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

// Adapter to use random_value() as a noise::module::Module
class UniformNoise : public noise::module::Module {
public:
    UniformNoise() : Module(0) {}

    int GetSourceModuleCount() const override { return 0; }

    double GetValue(double /*x*/, double /*y*/, double /*z*/) const override {
        // Return value in range [-1, 1] to match libnoise convention
        return random_value() * 2.0 - 1.0;
    }
};

// Factory function to create noise module based on config
static std::unique_ptr<noise::module::Module> get_noise_module(const PrintRegionConfig &config)
{
    const NoiseType noise_type = config.fuzzy_skin_noise_type.value;
    const double    scale      = config.fuzzy_skin_scale.value;
    const int       octaves    = config.fuzzy_skin_octaves.value;
    const double    persistence = config.fuzzy_skin_persistence.value;

    switch (noise_type) {
    case NoiseType::Perlin: {
        auto module = std::make_unique<noise::module::Perlin>();
        module->SetFrequency(1.0 / scale);
        module->SetOctaveCount(octaves);
        module->SetPersistence(persistence);
        return module;
    }
    case NoiseType::Billow: {
        auto module = std::make_unique<noise::module::Billow>();
        module->SetFrequency(1.0 / scale);
        module->SetOctaveCount(octaves);
        module->SetPersistence(persistence);
        return module;
    }
    case NoiseType::RidgedMulti: {
        auto module = std::make_unique<noise::module::RidgedMulti>();
        module->SetFrequency(1.0 / scale);
        module->SetOctaveCount(octaves);
        return module;
    }
    case NoiseType::Voronoi: {
        auto module = std::make_unique<noise::module::Voronoi>();
        module->SetFrequency(1.0 / scale);
        module->EnableDistance(true);
        return module;
    }
    case NoiseType::Classic:
    default:
        return std::make_unique<UniformNoise>();
    }
}

// Get noise value at a point, returning value in range [-1, 1]
static double get_noise_value(const noise::module::Module &noise_module, double x, double y, double z)
{
    double value = noise_module.GetValue(x, y, z);
    // Clamp to [-1, 1] as some noise types can exceed this range
    return std::clamp(value, -1.0, 1.0);
}

// Internal implementation for fuzzy polyline with noise support
static void fuzzy_polyline_impl(Points &poly, const bool closed, coordf_t slice_z, const PrintRegionConfig &config)
{
    const double fuzzy_skin_thickness      = scaled<double>(config.fuzzy_skin_thickness.value);
    const double fuzzy_skin_point_distance = scaled<double>(config.fuzzy_skin_point_dist.value);

    const double min_dist_between_points = fuzzy_skin_point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double       dist_left_over          = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point

    auto noise_module = get_noise_module(config);

    Points out;
    out.reserve(poly.size());

    // Skip the first point for open polyline.
    Point *p0 = closed ? &poly.back() : &poly.front();
    for (auto it_pt1 = closed ? poly.begin() : std::next(poly.begin()); it_pt1 != poly.end(); ++it_pt1) {
        Point &p1 = *it_pt1;

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist) {
            // Calculate position along the line
            Vec2d point_pos = p0->cast<double>() + p0p1 * (p0pa_dist / p0p1_size);

            // Get noise value at this position
            double noise_val = get_noise_value(*noise_module, unscale<double>(point_pos.x()), unscale<double>(point_pos.y()), slice_z);
            double r = noise_val * fuzzy_skin_thickness;

            out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0) {
            break;
        }

        --point_idx;
    }

    if (out.size() >= 3) {
        poly = std::move(out);
    }
}

void fuzzy_polygon(Polygon &polygon, coordf_t slice_z, const PrintRegionConfig &config)
{
    fuzzy_polyline_impl(polygon.points, true, slice_z, config);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, coordf_t slice_z, const PrintRegionConfig &config)
{
    const FuzzySkinMode mode = config.fuzzy_skin_mode.value;

    const double fuzzy_skin_thickness      = scaled<double>(config.fuzzy_skin_thickness.value);
    const double fuzzy_skin_point_distance = scaled<double>(config.fuzzy_skin_point_dist.value);

    const double min_dist_between_points = fuzzy_skin_point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double       dist_left_over          = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point

    auto noise_module = get_noise_module(config);

    Arachne::ExtrusionJunction *p0 = &ext_lines.front();
    Arachne::ExtrusionJunctions out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines) {
        if (p0->p == p1.p) {
            // Copy the first point.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist) {
            // Calculate position along the line
            Vec2d point_pos = p0->p.cast<double>() + p0p1 * (p0pa_dist / p0p1_size);

            // Get noise value at this position
            double noise_val = get_noise_value(*noise_module, unscale<double>(point_pos.x()), unscale<double>(point_pos.y()), slice_z);

            // Calculate displacement
            double displacement = 0.0;
            if (mode == FuzzySkinMode::Displacement || mode == FuzzySkinMode::Combined) {
                displacement = noise_val * fuzzy_skin_thickness;
            }

            // Calculate width variation
            coord_t new_width = p1.w;
            if (mode == FuzzySkinMode::Extrusion || mode == FuzzySkinMode::Combined) {
                // Vary width by up to ±thickness (as a fraction of original width)
                double width_factor = 1.0 + noise_val * (fuzzy_skin_thickness / std::max(1.0, static_cast<double>(p1.w)));
                width_factor = std::clamp(width_factor, 0.5, 1.5); // Clamp to reasonable range
                new_width = static_cast<coord_t>(p1.w * width_factor);
            }

            Point new_point = p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * displacement).cast<coord_t>();
            out.emplace_back(new_point, new_width, p1.perimeter_index);
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0) {
            break;
        }

        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) {
        // Connect endpoints.
        out.front().p = out.back().p;
    }

    if (out.size() >= 3) {
        ext_lines.junctions = std::move(out);
    }
}

bool should_fuzzify(const PrintRegionConfig &config, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    const FuzzySkinType fuzzy_skin_type = config.fuzzy_skin.value;

    if (fuzzy_skin_type == FuzzySkinType::None) {
        return false;
    }

    // Check first layer option
    if (layer_idx == 0 && !config.fuzzy_skin_first_layer.value) {
        return false;
    }

    // AllWalls: fuzzify all perimeters (internal and external)
    if (fuzzy_skin_type == FuzzySkinType::AllWalls) {
        return true;
    }

    // External: only outermost perimeter (perimeter_idx == 0) and only contours
    const bool fuzzify_contours = perimeter_idx == 0;

    // All: outermost perimeter for both contours and holes
    const bool fuzzify_holes = fuzzify_contours && fuzzy_skin_type == FuzzySkinType::All;

    return is_contour ? fuzzify_contours : fuzzify_holes;
}

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions,
                         const size_t layer_idx, coordf_t slice_z, const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    auto apply_fuzzy_skin_on_polygon = [&layer_idx, &slice_z, &perimeter_idx, &is_contour](const Polygon &polygon, const PrintRegionConfig &config) -> Polygon {
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            Polygon fuzzified_polygon = polygon;
            fuzzy_polygon(fuzzified_polygon, slice_z, config);

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
            fuzzy_polyline_impl(segment.polyline.points, false, slice_z, config);
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

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, const size_t layer_idx, coordf_t slice_z,
                                        const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    if (perimeter_regions.empty()) {
        if (should_fuzzify(base_config, layer_idx, perimeter_idx, is_contour)) {
            ExtrusionLine fuzzified_extrusion = extrusion;
            fuzzy_extrusion_line(fuzzified_extrusion, slice_z, base_config);

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
            fuzzy_extrusion_line(segment.extrusion, slice_z, config);
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
