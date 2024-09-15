///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "OpenVDBUtils.hpp"

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned 
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/Grid.h>
#include <openvdb/Metadata.h>
#include <openvdb/Types.h>
#include <openvdb/math/Transform.h>
#include <openvdb/math/Vec3.h>
#include <openvdb/math/Vec4.h>
#include <openvdb/tools/Morphology.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetRebuild.h>
#include <openvdb/tools/FastSweeping.h>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>
#include <cstddef>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

struct VoxelGrid
{
    openvdb::FloatGrid grid;

    mutable std::optional<openvdb::FloatGrid::ConstAccessor> accessor;

    template<class...Args>
    VoxelGrid(Args &&...args): grid{std::forward<Args>(args)...} {}
};

void VoxelGridDeleter::operator()(VoxelGrid *ptr) { delete ptr; }

// Similarly to std::make_unique()
template<class...Args>
VoxelGridPtr make_voxelgrid(Args &&...args)
{
    VoxelGrid *ptr = nullptr;
    try {
        ptr = new VoxelGrid(std::forward<Args>(args)...);
    } catch(...) {
        delete ptr;
    }

    return VoxelGridPtr{ptr};
}

template VoxelGridPtr make_voxelgrid<>();

inline Vec3f to_vec3f(const openvdb::Vec3s &v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d to_vec3d(const openvdb::Vec3s &v) { return to_vec3f(v).cast<double>(); }
inline Vec3i to_vec3i(const openvdb::Vec3I &v) { return Vec3i{int(v[2]), int(v[1]), int(v[0])}; }

class TriangleMeshDataAdapter {
public:
    const indexed_triangle_set &its;
    Transform3d trafo;

    size_t polygonCount() const { return its.indices.size(); }
    size_t pointCount() const   { return its.vertices.size(); }
    size_t vertexCount(size_t) const { return 3; }

    // Return position pos in local grid index space for polygon n and vertex v
    // The actual mesh will appear to openvdb as scaled uniformly by voxel_size
    // And the voxel count per unit volume can be affected this way.
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const
    {
        auto vidx = size_t(its.indices[n](Eigen::Index(v)));
        Slic3r::Vec3d p = trafo * its.vertices[vidx].cast<double>();
        pos = {p.x(), p.y(), p.z()};
    }

    TriangleMeshDataAdapter(const indexed_triangle_set &m, const Transform3d tr = Transform3d::Identity())
        : its{m}, trafo{tr} {}
};

struct Interrupter
{
    std::function<bool(int)> statusfn;

    void start(const char* name = nullptr) { (void)name; }
    void end() {}

    inline bool wasInterrupted(int percent = -1) { return statusfn && statusfn(percent); }
};

VoxelGridPtr mesh_to_grid(const indexed_triangle_set &mesh,
                          const MeshToGridParams &params)
{
    // Might not be needed but this is now proven to be working
    openvdb::initialize();

    std::vector<indexed_triangle_set> meshparts = its_split(mesh);

    auto it = std::remove_if(meshparts.begin(), meshparts.end(),
                             [](auto &m) {
                                 return its_volume(m) < EPSILON;
                             });

    meshparts.erase(it, meshparts.end());

    Transform3d trafo = params.trafo().cast<double>();
    trafo.prescale(params.voxel_scale());

    Interrupter interrupter{params.statusfn()};

    openvdb::FloatGrid::Ptr grid;
    for (auto &m : meshparts) {
        auto subgrid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            interrupter,
            TriangleMeshDataAdapter{m, trafo},
            openvdb::math::Transform{},
            params.exterior_bandwidth(),
            params.interior_bandwidth());

        if (interrupter.wasInterrupted())
            break;

        if (grid && subgrid)
            openvdb::tools::csgUnion(*grid, *subgrid);
        else if (subgrid)
            grid = std::move(subgrid);
    }

    if (interrupter.wasInterrupted())
        return {};

    if (meshparts.empty()) {
        // Splitting failed, fall back to hollow the original mesh
        grid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            interrupter,
            TriangleMeshDataAdapter{mesh, trafo},
            openvdb::math::Transform{},
            params.exterior_bandwidth(),
            params.interior_bandwidth());
    }

    if (interrupter.wasInterrupted())
        return {};

    grid->transform().preScale(1./params.voxel_scale());
    grid->insertMeta("voxel_scale", openvdb::FloatMetadata(params.voxel_scale()));

    VoxelGridPtr ret = make_voxelgrid(std::move(*grid));

    return ret;
}

indexed_triangle_set grid_to_mesh(const VoxelGrid &vgrid,
                                  double           isovalue,
                                  double           adaptivity,
                                  bool             relaxDisorientedTriangles)
{
    openvdb::initialize();

    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;

    auto &grid = vgrid.grid;

    openvdb::tools::volumeToMesh(grid, points, triangles, quads, isovalue,
                                 adaptivity, relaxDisorientedTriangles);

    indexed_triangle_set ret;
    ret.vertices.reserve(points.size());
    ret.indices.reserve(triangles.size() + quads.size() * 2);

    for (auto &v : points) ret.vertices.emplace_back(to_vec3f(v) /*/ scale*/);
    for (auto &v : triangles) ret.indices.emplace_back(to_vec3i(v));
    for (auto &quad : quads) {
        ret.indices.emplace_back(quad(2), quad(1), quad(0));
        ret.indices.emplace_back(quad(3), quad(2), quad(0));
    }

    return ret;
}

VoxelGridPtr dilate_grid(const VoxelGrid &vgrid,
                         float            exteriorBandWidth,
                         float            interiorBandWidth)
{
    constexpr int DilateIterations = 1;

    openvdb::FloatGrid::Ptr new_grid;

    float scale = get_voxel_scale(vgrid);

    if (interiorBandWidth > 0.f)
        new_grid = openvdb::tools::dilateSdf(
            vgrid.grid, scale * interiorBandWidth, openvdb::tools::NN_FACE_EDGE,
            DilateIterations,
            openvdb::tools::FastSweepingDomain::SWEEP_LESS_THAN_ISOVALUE);

    auto &arg = new_grid? *new_grid : vgrid.grid;

    if (exteriorBandWidth > 0.f)
        new_grid = openvdb::tools::dilateSdf(
            arg, scale * exteriorBandWidth, openvdb::tools::NN_FACE_EDGE,
            DilateIterations,
            openvdb::tools::FastSweepingDomain::SWEEP_GREATER_THAN_ISOVALUE);

    VoxelGridPtr ret;

    if (new_grid)
        ret = make_voxelgrid(std::move(*new_grid));
    else
        ret = make_voxelgrid(vgrid.grid);

    // Copies voxel_scale metadata, if it exists.
    ret->grid.insertMeta(*vgrid.grid.deepCopyMeta());

    return ret;
}

VoxelGridPtr redistance_grid(const VoxelGrid &vgrid,
                     float              iso,
                     float              er,
                     float              ir)
{
    auto new_grid = openvdb::tools::levelSetRebuild(vgrid.grid, iso, er, ir);

    auto ret = make_voxelgrid(std::move(*new_grid));

    // Copies voxel_scale metadata, if it exists.
    ret->grid.insertMeta(*vgrid.grid.deepCopyMeta());

    return ret;
}

VoxelGridPtr redistance_grid(const VoxelGrid &vgrid, float iso)
{
    auto new_grid = openvdb::tools::levelSetRebuild(vgrid.grid, iso);

    auto ret = make_voxelgrid(std::move(*new_grid));

    // Copies voxel_scale metadata, if it exists.
    ret->grid.insertMeta(*vgrid.grid.deepCopyMeta());

    return ret;
}

void grid_union(VoxelGrid &grid, VoxelGrid &arg)
{
    openvdb::tools::csgUnion(grid.grid, arg.grid);
}

void grid_difference(VoxelGrid &grid, VoxelGrid &arg)
{
    openvdb::tools::csgDifference(grid.grid, arg.grid);
}

void grid_intersection(VoxelGrid &grid, VoxelGrid &arg)
{
    openvdb::tools::csgIntersection(grid.grid, arg.grid);
}

void reset_accessor(const VoxelGrid &vgrid)
{
    vgrid.accessor = vgrid.grid.getConstAccessor();
}

double get_distance_raw(const Vec3f &p, const VoxelGrid &vgrid)
{
    if (!vgrid.accessor)
        reset_accessor(vgrid);

    auto v       = (p).cast<double>();
    auto grididx = vgrid.grid.transform().worldToIndexCellCentered(
        {v.x(), v.y(), v.z()});

    return vgrid.accessor->getValue(grididx) ;
}

float get_voxel_scale(const VoxelGrid &vgrid)
{
    float scale = 1.;
    try {
        scale = vgrid.grid.template metaValue<float>("voxel_scale");
    }  catch (...) { }

    return scale;
}

VoxelGridPtr clone(const VoxelGrid &grid)
{
    return make_voxelgrid(grid);
}

void rescale_grid(VoxelGrid &grid, float scale)
{/*
    float old_scale = get_voxel_scale(grid);

    float nscale = scale / old_scale;*/
//    auto tr = openvdb::math::Transform::createLinearTransform(scale);
    grid.grid.transform().preScale(scale);

//    grid.grid.insertMeta("voxel_scale", openvdb::FloatMetadata(nscale));

//    grid.grid.setTransform(tr);
}

bool is_grid_empty(const VoxelGrid &grid)
{
    return grid.grid.empty();
}

} // namespace Slic3r
