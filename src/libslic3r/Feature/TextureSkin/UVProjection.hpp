///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_TextureSkin_UVProjection_hpp_
#define libslic3r_TextureSkin_UVProjection_hpp_

#include <array>
#include <cstdint>

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r::Feature::TextureSkin {

// Seven UV projection modes, matching stlTexturizer/js/mapping.js.
enum class UVMode : uint8_t {
    PlanarXY    = 0,
    PlanarXZ    = 1,
    PlanarYZ    = 2,
    Cylindrical = 3,
    Spherical   = 4,
    Triplanar   = 5,
    Cubic       = 6,
};

struct UVSettings {
    double scale_u         = 1.0;   // user scale; lower = texture repeats more often
    double scale_v         = 1.0;
    double offset_u        = 0.0;
    double offset_v        = 0.0;
    double rotation_deg    = 0.0;
    double texture_aspect_u= 1.0;   // aspect of the texture image (used to normalise)
    double texture_aspect_v= 1.0;
    double mapping_blend   = 0.0;   // 0 = hard, 1 = soft triplanar-style blend
    double seam_band_width = 0.35;  // width of the seam-blend band (cubic/cylindrical/spherical)
    double cap_angle_deg   = 20.0;  // cylindrical-cap threshold
};

// A single UV sample with a blend weight. Most modes return a single sample
// (w=1.0), but triplanar/cubic/cylindrical-with-blend return 2-3 samples that
// the caller weights and accumulates.
struct UVSample {
    double u = 0.0;
    double v = 0.0;
    double w = 1.0;
};

// Up to 4 blended samples (triplanar ≤3 + cylindrical cap extras).
struct UVResult {
    std::array<UVSample, 4> samples{};
    int count = 0;
};

// Compute UV coordinate(s) for a point on the object surface.
//   pos    : 3D object-local position.
//   normal : surface normal at pos (unit). For slice-time texturing the 2D
//            perimeter outward-normal is promoted to 3D with z=0.
//   mode   : projection mode.
//   settings : user UV transform parameters.
//   bounds : object-local bounding box of the volume.
UVResult compute_uv(const Vec3d &pos, const Vec3d &normal, UVMode mode,
                    const UVSettings &settings, const BoundingBoxf3 &bounds);

} // namespace Slic3r::Feature::TextureSkin

#endif // libslic3r_TextureSkin_UVProjection_hpp_
