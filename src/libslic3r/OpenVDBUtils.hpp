///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef OPENVDBUTILS_HPP
#define OPENVDBUTILS_HPP

#include <libslic3r/TriangleMesh.hpp>
#include <functional>
#include <memory>

#include "admesh/stl.h"
#include "libslic3r/Point.hpp"

namespace Slic3r {

struct VoxelGrid;

struct VoxelGridDeleter { void operator()(VoxelGrid *ptr); };
using VoxelGridPtr = std::unique_ptr<VoxelGrid, VoxelGridDeleter>;

// This is like std::make_unique for a voxelgrid
template<class... Args> VoxelGridPtr make_voxelgrid(Args &&...args);

// Default constructed voxelgrid can be obtained this way.
extern template VoxelGridPtr make_voxelgrid<>();

void reset_accessor(const VoxelGrid &vgrid);

double get_distance_raw(const Vec3f &p, const VoxelGrid &interior);

float get_voxel_scale(const VoxelGrid &grid);

VoxelGridPtr clone(const VoxelGrid &grid);

class MeshToGridParams {
    Transform3f        m_tr                = Transform3f::Identity();
    float              m_voxel_scale       = 1.f;
    float              m_exteriorBandWidth = 3.0f;
    float              m_interiorBandWidth = 3.0f;

    std::function<bool(int)> m_statusfn;

public:
    MeshToGridParams & trafo(const Transform3f &v) { m_tr = v; return *this; }
    MeshToGridParams & voxel_scale(float v) { m_voxel_scale = v; return *this; }
    MeshToGridParams & exterior_bandwidth(float v) { m_exteriorBandWidth = v; return *this; }
    MeshToGridParams & interior_bandwidth(float v) { m_interiorBandWidth = v; return *this; }
    MeshToGridParams & statusfn(std::function<bool(int)> fn) { m_statusfn = fn; return *this; }

    const Transform3f& trafo() const noexcept { return m_tr; }
    float voxel_scale() const noexcept { return m_voxel_scale; }
    float exterior_bandwidth() const noexcept { return m_exteriorBandWidth; }
    float interior_bandwidth() const noexcept { return m_interiorBandWidth; }
    const std::function<bool(int)>& statusfn() const noexcept { return m_statusfn; }
};

// Here voxel_scale defines the scaling of voxels which affects the voxel count.
// 1.0 value means a voxel for every unit cube. 2 means the model is scaled to
// be 2x larger and the voxel count is increased by the increment in the scaled
// volume, thus 4 times. This kind a sampling accuracy selection is not
// achievable through the Transform parameter. (TODO: or is it?)
// The resulting grid will contain the voxel_scale in its metadata under the
// "voxel_scale" key to be used in grid_to_mesh function.
VoxelGridPtr mesh_to_grid(const indexed_triangle_set &mesh,
                          const MeshToGridParams &params = {});

indexed_triangle_set grid_to_mesh(const VoxelGrid &grid,
                                  double           isovalue      = 0.0,
                                  double           adaptivity    = 0.0,
                                  bool relaxDisorientedTriangles = true);

VoxelGridPtr dilate_grid(const VoxelGrid &grid,
                         float            exteriorBandWidth = 3.0f,
                         float            interiorBandWidth = 3.0f);

VoxelGridPtr redistance_grid(const VoxelGrid &grid, float iso);

VoxelGridPtr redistance_grid(const VoxelGrid &grid,
                             float            iso,
                             float            ext_range,
                             float            int_range);

void rescale_grid(VoxelGrid &grid, float scale);

void grid_union(VoxelGrid &grid, VoxelGrid &arg);
void grid_difference(VoxelGrid &grid, VoxelGrid &arg);
void grid_intersection(VoxelGrid &grid, VoxelGrid &arg);

bool is_grid_empty(const VoxelGrid &grid);

} // namespace Slic3r

#endif // OPENVDBUTILS_HPP
