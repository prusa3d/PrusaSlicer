#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r::PerimeterGenerator {
struct Parameters;
} // namespace Slic3r::PerimeterGenerator

namespace Slic3r::Feature::FuzzySkin {

void fuzzy_polygon(Polygon &polygon, double fuzzy_skin_thickness, double fuzzy_skin_point_distance);

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist);

bool should_fuzzify(const PrintRegionConfig &config, size_t layer_idx, size_t perimeter_idx, bool is_contour);

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx, bool is_contour);

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx, bool is_contour);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
