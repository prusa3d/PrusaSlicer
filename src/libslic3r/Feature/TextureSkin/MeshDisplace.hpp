///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_TextureSkin_MeshDisplace_hpp_
#define libslic3r_TextureSkin_MeshDisplace_hpp_

#include <cstdint>
#include <functional>
#include <vector>

#include "admesh/stl.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include "TextureSampler.hpp"
#include "UVProjection.hpp"

namespace Slic3r {
namespace Feature::TextureSkin {

struct MeshDisplaceParams {
    const GrayImage *image = nullptr;       // non-owning, required
    UVMode          uv_mode = UVMode::Triplanar;
    UVSettings      uv_settings;
    float           amplitude_mm          = 0.5f;
    float           edge_length_mm        = 0.4f;  // subdivide edges longer than this
    uint32_t        target_triangle_count = 100000;
    float           max_error             = 1e6f;  // QEM cap, essentially unbounded
    BoundingBoxf3   bounds;                          // volume-local bounds in mm

    // If non-null + non-empty: displace only the vertices whose incident
    // original facets are marked painted. Must be sized to
    // original_its.indices.size(). If null/empty: displace all vertices.
    const std::vector<bool> *painted_face_mask = nullptr;

    // Finer-grained mask: painted sub-facet ITS in original mesh coords
    // (from FacetsAnnotation::get_facets). When provided, overrides
    // painted_face_mask — a vertex is displaced only if it lies inside one
    // of these painted sub-triangles (3D barycentric + plane-distance test).
    const indexed_triangle_set *painted_region_its = nullptr;

    // Skip displacement for vertices whose smooth normal points downward
    // (into the build plate). Normal Z component threshold: a vertex is
    // excluded if smooth_normal.z < bottom_threshold.
    bool  skip_bottom_face  = false;
    float bottom_threshold  = -0.7f;
};

// Perform subdivide → displace → decimate. Returns the new mesh, or an empty
// ITS if cancelled.
indexed_triangle_set mesh_displace(
    const indexed_triangle_set                 &original_its,
    const MeshDisplaceParams                   &params,
    std::function<void()>                       throw_on_cancel = nullptr,
    std::function<void(int /*percent 0..100*/)> statusfn        = nullptr);

// Extracts a per-original-facet binary paint mask from a FacetsAnnotation's
// splitting data. Safe: returns all-false if nothing is painted.
std::vector<bool> extract_painted_face_mask(
    size_t                                               num_original_facets,
    const TriangleSelector::TriangleSplittingData       &data);

} // namespace Feature::TextureSkin
} // namespace Slic3r

#endif // libslic3r_TextureSkin_MeshDisplace_hpp_
