#ifndef libslic3r_TextureSkin_hpp_
#define libslic3r_TextureSkin_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polygon.hpp"

#include "TexturePatterns.hpp"
#include "UVProjection.hpp"

namespace Slic3r {
class PrintRegionConfig;
} // namespace Slic3r

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r {
struct PerimeterRegion;
using PerimeterRegions = std::vector<PerimeterRegion>;
} // namespace Slic3r

namespace Slic3r::Feature::TextureSkin {

// Complete parameter bundle for a single texture-skin application.
// Values with coord_t (amplitude, point_distance, layer_z) are in scaled units.
struct Params {
    const GrayImage *image = nullptr;   // non-owning
    UVMode           mode  = UVMode::Triplanar;
    UVSettings       uv;
    double           amplitude_mm  = 0.3;  // ± peak displacement in mm
    double           point_dist_mm = 0.8;  // insertion distance in mm
    BoundingBoxf3    bounds;               // object-local bounds in mm
    double           layer_z_mm   = 0.0;   // world Z for this layer in mm
};

// Core per-polyline texturing. Re-samples `poly` inserting points every
// `point_dist_mm` and perturbs them along the 2D perpendicular by the texture
// value at their 3D position.
void texturize_polyline(Points &poly, bool closed, const Params &params);

// Polygon wrapper (closed).
void texturize_polygon(Polygon &polygon, const Params &params);

// Arachne extrusion line texturing.
void texturize_extrusion_line(Arachne::ExtrusionLine &ext_line, const Params &params);

// Gating predicate (mirrors FuzzySkin::should_fuzzify).
bool should_texturize(const PrintRegionConfig &config, size_t layer_idx, size_t perimeter_idx, bool is_contour);

// Resolve Params from a PrintRegionConfig. Returns false if texture skin is
// disabled or the selected pattern image could not be loaded.
bool params_from_config(const PrintRegionConfig &config,
                        const BoundingBoxf3 &volume_bounds,
                        double layer_z_mm,
                        Params &out);

// Apply texture skin to a polygon, honouring per-region config segmentation.
Polygon apply_texture_skin(const Polygon &polygon,
                           const PrintRegionConfig &base_config,
                           const PerimeterRegions &perimeter_regions,
                           size_t layer_idx, size_t perimeter_idx, bool is_contour,
                           const BoundingBoxf3 &volume_bounds,
                           double layer_z_mm);

// Apply texture skin to an Arachne extrusion line.
Arachne::ExtrusionLine apply_texture_skin(const Arachne::ExtrusionLine &extrusion,
                                          const PrintRegionConfig &base_config,
                                          const PerimeterRegions &perimeter_regions,
                                          size_t layer_idx, size_t perimeter_idx, bool is_contour,
                                          const BoundingBoxf3 &volume_bounds,
                                          double layer_z_mm);

// Sample the texture at a 3D world position and return a displacement value in
// millimetres. Exposed for unit tests and for non-polyline callers.
double sample_displacement_mm(const Vec3d &pos_mm, const Vec3d &normal, const Params &params);

} // namespace Slic3r::Feature::TextureSkin

#endif // libslic3r_TextureSkin_hpp_
