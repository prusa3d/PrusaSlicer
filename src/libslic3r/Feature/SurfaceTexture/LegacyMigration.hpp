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

} // namespace Slic3r::Feature::SurfaceTexture

#endif // libslic3r_SurfaceTexture_LegacyMigration_hpp_
