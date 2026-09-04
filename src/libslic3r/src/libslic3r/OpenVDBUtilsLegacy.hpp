#ifndef OPENVDBUTILSLEGACY_HPP
#define OPENVDBUTILSLEGACY_HPP

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/FastSweeping.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetRebuild.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

namespace Slic3r {

openvdb::FloatGrid::Ptr mesh_to_grid(const indexed_triangle_set &    mesh,
                                     const openvdb::math::Transform &tr,
                                     float voxel_scale,
                                     float exteriorBandWidth,
                                     float interiorBandWidth)
{
    class TriangleMeshDataAdapter {
    public:
        const indexed_triangle_set &its;
        float voxel_scale;

        size_t polygonCount() const { return its.indices.size(); }
        size_t pointCount() const   { return its.vertices.size(); }
        size_t vertexCount(size_t) const { return 3; }

             // Return position pos in local grid index space for polygon n and vertex v
             // The actual mesh will appear to openvdb as scaled uniformly by voxel_size
             // And the voxel count per unit volume can be affected this way.
        void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const
        {
            auto vidx = size_t(its.indices[n](Eigen::Index(v)));
            Slic3r::Vec3d p = its.vertices[vidx].cast<double>() * voxel_scale;
            pos = {p.x(), p.y(), p.z()};
        }

        TriangleMeshDataAdapter(const indexed_triangle_set &m, float voxel_sc = 1.f)
            : its{m}, voxel_scale{voxel_sc} {};
    };

    // Might not be needed but this is now proven to be working
    openvdb::initialize();

    std::vector<indexed_triangle_set> meshparts = its_split(mesh);

    auto it = std::remove_if(meshparts.begin(), meshparts.end(),
                             [](auto &m) { return its_volume(m) < EPSILON; });

    meshparts.erase(it, meshparts.end());

    openvdb::FloatGrid::Ptr grid;
    for (auto &m : meshparts) {
        auto subgrid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            TriangleMeshDataAdapter{m, voxel_scale}, tr, 1.f, 1.f);

        if (grid && subgrid) openvdb::tools::csgUnion(*grid, *subgrid);
        else if (subgrid) grid = std::move(subgrid);
    }

    if (meshparts.size() > 1) {
        // This is needed to avoid various artefacts on multipart meshes.
        // TODO: replace with something faster
        grid = openvdb::tools::levelSetRebuild(*grid, 0., 1.f, 1.f);
    }
    if(meshparts.empty()) {
        // Splitting failed, fall back to hollow the original mesh
        grid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            TriangleMeshDataAdapter{mesh}, tr, 1.f, 1.f);
    }

    constexpr int DilateIterations = 1;

    grid = openvdb::tools::dilateSdf(
        *grid, interiorBandWidth, openvdb::tools::NN_FACE_EDGE,
        DilateIterations,
        openvdb::tools::FastSweepingDomain::SWEEP_LESS_THAN_ISOVALUE);

    grid = openvdb::tools::dilateSdf(
        *grid, exteriorBandWidth, openvdb::tools::NN_FACE_EDGE,
        DilateIterations,
        openvdb::tools::FastSweepingDomain::SWEEP_GREATER_THAN_ISOVALUE);

    grid->insertMeta("voxel_scale", openvdb::FloatMetadata(voxel_scale));

    return grid;
}

} // namespace Slic3r

#endif // OPENVDBUTILSLEGACY_HPP
