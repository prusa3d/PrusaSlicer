///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLA_HOLLOWING_HPP
#define SLA_HOLLOWING_HPP

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/OpenVDBUtils.hpp>
#include <libslic3r/SLA/JobController.hpp>
#include <libslic3r/CSGMesh/VoxelizeCSGMesh.hpp>
#include <stddef.h>
#include <memory>
#include <algorithm>
#include <array>
#include <functional>
#include <utility>
#include <vector>
#include <cstddef>

#include "admesh/stl.h"
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

class ModelObject;
struct VoxelGrid;

namespace sla {

struct HollowingConfig
{
    double min_thickness    = 2.;
    double quality          = 0.5;
    double closing_distance = 0.5;
    bool enabled = true;
};

enum HollowingFlags { hfRemoveInsideTriangles = 0x1 };

// All data related to a generated mesh interior. Includes the 3D grid and mesh
// and various metadata. No need to manipulate from outside.
struct Interior;

struct InteriorDeleter { void operator()(Interior *p); };
using  InteriorPtr = std::unique_ptr<Interior, InteriorDeleter>;

indexed_triangle_set &      get_mesh(Interior &interior);
const indexed_triangle_set &get_mesh(const Interior &interior);

const VoxelGrid & get_grid(const Interior &interior);
VoxelGrid &get_grid(Interior &interior);

struct DrainHole
{
    Vec3f pos;
    Vec3f normal;
    float radius;
    float height;
    bool  failed = false;

    DrainHole()
        : pos(Vec3f::Zero()), normal(Vec3f::UnitZ()), radius(5.f), height(10.f)
    {}

    DrainHole(Vec3f p, Vec3f n, float r, float h, bool fl = false)
        : pos(p), normal(n), radius(r), height(h), failed(fl)
    {}

    DrainHole(const DrainHole& rhs) :
        DrainHole(rhs.pos, rhs.normal, rhs.radius, rhs.height, rhs.failed) {}

    bool operator==(const DrainHole &sp) const;

    bool operator!=(const DrainHole &sp) const { return !(sp == (*this)); }

    bool is_inside(const Vec3f& pt) const;

    bool get_intersections(const Vec3f& s, const Vec3f& dir,
                           std::array<std::pair<float, Vec3d>, 2>& out) const;

    indexed_triangle_set to_mesh() const;

    template<class Archive> inline void serialize(Archive &ar)
    {
        ar(pos, normal, radius, height, failed);
    }

    static constexpr size_t steps = 32;
};

using DrainHoles = std::vector<DrainHole>;

constexpr float HoleStickOutLength = 1.f;

constexpr float IsoAtZero = 0.f;

double get_voxel_scale(double mesh_volume, const HollowingConfig &hc);

InteriorPtr generate_interior(const VoxelGrid &mesh,
                              const HollowingConfig &  = {},
                              const JobController &ctl = {});

// Return the maximum possible volume (upper bound) of a csg mesh.
// Not the exact volume, that would require actually doing the booleans.
template<class Cont> double csgmesh_positive_maxvolume(const Cont &csg)
{
    double mesh_vol = 0;

    bool skip = false;
    for (const auto &m : csg) {
        auto op = csg::get_operation(m);
        auto stackop = csg::get_stack_operation(m);
        if (stackop == csg::CSGStackOp::Push && op != csg::CSGType::Union)
            skip = true;

        if (!skip && csg::get_mesh(m) && op == csg::CSGType::Union)
            mesh_vol = std::max(mesh_vol,
                                double(its_volume(*(csg::get_mesh(m)))));

        if (stackop == csg::CSGStackOp::Pop)
            skip = false;
    }

    return mesh_vol;
}

template<class It>
InteriorPtr generate_interior(const Range<It>       &csgparts,
                              const HollowingConfig &hc  = {},
                              const JobController   &ctl = {})
{
    double mesh_vol = csgmesh_positive_maxvolume(csgparts);
    double voxsc    = get_voxel_scale(mesh_vol, hc);

    auto params = csg::VoxelizeParams{}
                      .voxel_scale(voxsc)
                      .exterior_bandwidth(3.f)
                      .interior_bandwidth(3.f)
                      .statusfn([&ctl](int){
                          return ctl.stopcondition && ctl.stopcondition();
                      });

    auto ptr = csg::voxelize_csgmesh(csgparts, params);

    if (!ptr || (ctl.stopcondition && ctl.stopcondition()))
        return {};

    ptr = redistance_grid(*ptr, IsoAtZero,
                          params.exterior_bandwidth(),
                          params.interior_bandwidth());

    return ptr ? generate_interior(*ptr, hc, ctl) :
                 InteriorPtr{};
}

inline InteriorPtr generate_interior(const indexed_triangle_set &mesh,
                                     const HollowingConfig &hc = {},
                                     const JobController &ctl = {})
{
    auto csgmesh = std::array{ csg::CSGPart{&mesh} };

    return generate_interior(range(csgmesh), hc, ctl);
}

// Will do the hollowing
void hollow_mesh(TriangleMesh &mesh, const HollowingConfig &cfg, int flags = 0);

// Hollowing prepared in "interior", merge with original mesh
void hollow_mesh(TriangleMesh &mesh, const Interior &interior, int flags = 0);

// Will do the hollowing
void hollow_mesh(indexed_triangle_set &mesh, const HollowingConfig &cfg, int flags = 0);

// Hollowing prepared in "interior", merge with original mesh
void hollow_mesh(indexed_triangle_set &mesh, const Interior &interior, int flags = 0);

enum class HollowMeshResult {
    Ok = 0,
    FaultyMesh = 1,
    FaultyHoles = 2,
    DrillingFailed = 4
};

// Return HollowMeshResult codes OR-ed.
int hollow_mesh_and_drill(
    indexed_triangle_set &mesh,
    const Interior& interior,
    const DrainHoles &holes,
    std::function<void(size_t)> on_hole_fail = [](size_t){});

void remove_inside_triangles(TriangleMesh &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask = {});

void remove_inside_triangles(indexed_triangle_set &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask = {});

sla::DrainHoles transformed_drainhole_points(const ModelObject &mo,
                                             const Transform3d &trafo);

void cut_drainholes(std::vector<ExPolygons> & obj_slices,
                    const std::vector<float> &slicegrid,
                    float                     closing_radius,
                    const sla::DrainHoles &   holes,
                    std::function<void(void)> thr);

inline void swap_normals(indexed_triangle_set &its)
{
    for (auto &face : its.indices)
        std::swap(face(0), face(2));
}

// Create exclude mask for triangle removal inside hollowed interiors.
// This is necessary when the interior is already part of the mesh which was
// drilled using CGAL mesh boolean operation. Excluded will be the triangles
// originally part of the interior mesh and triangles that make up the drilled
// hole walls.
std::vector<bool> create_exclude_mask(
    const indexed_triangle_set &its,
    const sla::Interior &interior,
    const std::vector<sla::DrainHole> &holes);

} // namespace sla
} // namespace Slic3r

#endif // HOLLOWINGFILTER_H
