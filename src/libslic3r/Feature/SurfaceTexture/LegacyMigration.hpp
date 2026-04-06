///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_SurfaceTexture_LegacyMigration_hpp_
#define libslic3r_SurfaceTexture_LegacyMigration_hpp_

namespace Slic3r {
class ModelVolume;
}

namespace Slic3r::Feature::SurfaceTexture {

// Merge legacy fuzzy_skin_facets + texture_skin_facets into the unified
// surface_texture_facets annotation. Fuzzy sub-triangle precision is
// preserved (state bits are identical). Texture-painted triangles are
// migrated at whole-triangle granularity and override any fuzzy paint on
// the same original triangle (conflicts resolve to PATTERN).
//
// Safe to call repeatedly: no-ops if surface_texture_facets is already
// populated.
void migrate_legacy_paint_to_surface_texture(ModelVolume &volume);

// Reverse direction: rebuild legacy fuzzy_skin_facets + texture_skin_facets
// from surface_texture_facets. Used by PrintApply just before the slice-time
// segmentation pipeline runs, so the existing
// fuzzy_skin_segmentation_by_painting / texture_skin_segmentation_by_painting
// callers can stay unchanged.
void derive_legacy_from_surface_texture(ModelVolume &volume);

} // namespace Slic3r::Feature::SurfaceTexture

#endif // libslic3r_SurfaceTexture_LegacyMigration_hpp_
