#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PerimeterGenerator.hpp"

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r::Feature::FuzzySkin {

// Apply fuzzy skin displacement to a polygon
void fuzzy_polygon(Polygon &polygon, coordf_t slice_z, const PrintRegionConfig &config);

// Apply fuzzy skin to an extrusion line (supports displacement, extrusion width, or combined modes)
void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, coordf_t slice_z, const PrintRegionConfig &config);

// Determine if fuzzy skin should be applied based on config, layer, perimeter index, and contour type
bool should_fuzzify(const PrintRegionConfig &config, size_t layer_idx, size_t perimeter_idx, bool is_contour);

// Apply fuzzy skin to a polygon with region-aware processing
Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions,
                         size_t layer_idx, coordf_t slice_z, size_t perimeter_idx, bool is_contour);

// Apply fuzzy skin to an extrusion line with region-aware processing
Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, size_t layer_idx, coordf_t slice_z,
                                        size_t perimeter_idx, bool is_contour);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
