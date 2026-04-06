#include "LegacyMigration.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r::Feature::SurfaceTexture {

// Per-original-facet "has any paint" bitmask from a FacetsAnnotation's
// splitting data. A facet appears in triangles_to_split iff it has
// non-default state somewhere in its (possibly sub-divided) subtree.
static std::vector<bool> per_facet_painted(size_t num_facets,
                                           const TriangleSelector::TriangleSplittingData &data)
{
    std::vector<bool> painted(num_facets, false);
    for (const auto &m : data.triangles_to_split)
        if (m.triangle_idx >= 0 && size_t(m.triangle_idx) < num_facets)
            painted[m.triangle_idx] = true;
    return painted;
}

void migrate_legacy_paint_to_surface_texture(ModelVolume &volume)
{
    if (!volume.surface_texture_facets.empty()) return;
    if (volume.fuzzy_skin_facets.empty() && volume.texture_skin_facets.empty()) return;

    // Fuzzy data is bit-identical (FUZZY_SKIN == SURFACE_FUZZY == ENFORCER),
    // so we can copy its splitting data wholesale and keep sub-triangle
    // precision.
    volume.surface_texture_facets.assign(volume.fuzzy_skin_facets);

    // Overlay texture paint as whole-triangle SURFACE_PATTERN, overriding
    // any fuzzy paint on the same original triangle.
    if (!volume.texture_skin_facets.empty()) {
        const size_t num_facets = volume.mesh().its.indices.size();
        const auto texture_mask = per_facet_painted(num_facets,
                                                    volume.texture_skin_facets.get_data());
        TriangleSelector selector(volume.mesh());
        selector.deserialize(volume.surface_texture_facets.get_data(), false);
        for (size_t i = 0; i < num_facets; ++i)
            if (texture_mask[i])
                selector.set_facet(int(i), TriangleStateType::SURFACE_PATTERN);
        volume.surface_texture_facets.set(selector);
    }
}

} // namespace Slic3r::Feature::SurfaceTexture
