#include <libslic3r/OpenVDBUtils.hpp>
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/SLA/Hollowing.hpp>
#include <Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp>
#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include <libslic3r/Model.hpp>
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"
#include <Slic3r/Log.hpp>
#include "libslic3r/I18N_private.hpp"
#include <functional>
#include <numeric>
#include <unordered_set>
#include <random>
#include <cmath>
#include <mutex>
#include <string>
#include <cassert>
#include <cinttypes>

#include "Slic3r/Exception.hpp"
#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r {
struct VoxelGrid;
namespace execution = Slic3r::Biz::Algorithms::Execution;
namespace BB = Biz::Algorithms::BoundingBox;

using Biz::Algorithms::TriangleMesh::VertexFaceIndex;
using Domain::Index3;
using Domain::TriangleMesh;
namespace TriMesh = Biz::Algorithms::TriangleMesh;
namespace MeshBoolean = Biz::CGAL::Algorithms::MeshBoolean;
namespace AABBTreeIndirect = Biz::Algorithms::AABBTreeIndirect;

namespace sla {

struct Interior {
    indexed_triangle_set mesh;
    VoxelGridPtr gridptr;

    double iso_surface = 0.;
    double thickness = 0.;
    double full_narrowb = 2.;

    void reset_accessor() const  // This resets the accessor and its cache
    // Not a thread safe call!
    {
        if (gridptr)
            Slic3r::reset_accessor(*gridptr);
    }
};

void InteriorDeleter::operator()(Interior *p)
{
    delete p;
}

indexed_triangle_set &get_mesh(Interior &interior)
{
    return interior.mesh;
}

const indexed_triangle_set &get_mesh(const Interior &interior)
{
    return interior.mesh;
}

const VoxelGrid &get_grid(const Interior &interior)
{
    return *interior.gridptr;
}

VoxelGrid &get_grid(Interior &interior)
{
    return *interior.gridptr;
}

InteriorPtr generate_interior(const VoxelGrid       &vgrid,
                              const HollowingConfig &hc,
                              const JobController   &ctl)
{
    double voxsc    = get_voxel_scale(vgrid);
    double offset   = hc.min_thickness;              // world units
    double D        = hc.closing_distance;           // world units
    float  in_range = 1.1f * float(offset + D);      // world units
    float  out_range = 1.f / voxsc; // world units
    auto   narrowb  = 1.f;  // voxel units (voxel count)

    if (ctl.stopcondition()) return {};
    else ctl.statuscb(0, _u8L("Hollowing"));

    auto gridptr = dilate_grid(vgrid, out_range, in_range);

    if (ctl.stopcondition()) return {};
    else ctl.statuscb(30, _u8L("Hollowing"));

    double iso_surface = D;
    if (D > EPSILON) {
        gridptr = redistance_grid(*gridptr, -(offset + D), narrowb, narrowb);

        gridptr = dilate_grid(*gridptr, 1.1 * std::ceil(iso_surface), 0.f);

        out_range = iso_surface;
        in_range  = narrowb / voxsc;
    } else {
        iso_surface = -offset;
    }

    if (ctl.stopcondition()) return {};
    else ctl.statuscb(70, _u8L("Hollowing"));

    double adaptivity = 0.;
    InteriorPtr interior = InteriorPtr{new Interior{}};

    interior->mesh = grid_to_mesh(*gridptr, iso_surface, adaptivity);
    interior->gridptr = std::move(gridptr);

    if (ctl.stopcondition()) return {};
    else ctl.statuscb(100, _u8L("Hollowing"));

    interior->iso_surface = iso_surface;
    interior->thickness   = offset;
    interior->full_narrowb = (out_range + in_range) / 2.;

    return interior;
}

indexed_triangle_set to_mesh(const Domain::SLA::DrainHole& hole)
{
    auto r = double(hole.radius);
    auto h = double(hole.height);
    indexed_triangle_set result = TriMesh::its_make_cylinder(r, h); //sla::cylinder(r, h, steps);
    Eigen::Quaternionf q;
    q.setFromTwoVectors(Vec3f::UnitZ(), hole.normal);
    for(auto& p : result.vertices) p = q * p + hole.pos;

    return result;
}

void cut_drainholes(std::vector<ExPolygons> & obj_slices,
                    const std::vector<float> &slicegrid,
                    float                     closing_radius,
                    const Domain::SLA::DrainHoles &   holes,
                    std::function<void(void)> thr)
{
    using Biz::Algorithms::TriangleMesh::construct;

    TriangleMesh mesh;
    for (const Domain::SLA::DrainHole &holept : holes)
        mesh.merge(TriangleMesh{construct(to_mesh(holept))});
    
    if (mesh.empty()) return;
    
    std::vector<ExPolygons> hole_slices = slice_mesh_ex(mesh.its, slicegrid, closing_radius, thr);
    
    if (obj_slices.size() != hole_slices.size())
        SPDLOG_WARN("Sliced object and drain-holes layer count does not match!");

    size_t until = std::min(obj_slices.size(), hole_slices.size());
    
    for (size_t i = 0; i < until; ++i)
        obj_slices[i] = diff_ex(obj_slices[i], hole_slices[i]);
}

void hollow_mesh(TriangleMesh &mesh, const HollowingConfig &cfg, int flags)
{
    InteriorPtr interior = generate_interior(mesh.its, cfg, JobController{});
    if (!interior) return;

    hollow_mesh(mesh, *interior, flags);
}

void hollow_mesh(TriangleMesh &mesh, const Interior &interior, int flags)
{
    if (mesh.empty() || interior.mesh.empty()) return;

    if (flags & hfRemoveInsideTriangles && interior.gridptr)
        remove_inside_triangles(mesh, interior);

    indexed_triangle_set interi = interior.mesh;
    sla::swap_normals(interi);

    using Biz::Algorithms::TriangleMesh::construct;
    TriangleMesh inter{construct(std::move(interi))};

    mesh.merge(inter);
}

void hollow_mesh(indexed_triangle_set &mesh, const Interior &interior, int flags)
{
    if (mesh.empty() || interior.mesh.empty()) return;

    if (flags & hfRemoveInsideTriangles && interior.gridptr)
        remove_inside_triangles(mesh, interior);

    indexed_triangle_set interi = interior.mesh;
    sla::swap_normals(interi);

    Domain::its_merge(mesh, interi);
}

// Get the distance of p to the interior's zero iso_surface. Interior should
// have its zero isosurface positioned at offset + closing_distance inwards form
// the model surface.
static double get_distance_raw(const Vec3f &p, const Interior &interior)
{
    assert(interior.gridptr);

    return Slic3r::get_distance_raw(p, *interior.gridptr);
}

struct TriangleBubble { Vec3f center; double R; };

// Return the distance of bubble center to the interior boundary or NaN if the
// triangle is too big to be measured.
static double get_distance(const TriangleBubble &b, const Interior &interior)
{
    double R = b.R;
    double D = 2. * R;
    double Dst = get_distance_raw(b.center, interior);

    return D > interior.full_narrowb ||
           ((Dst - R) < 0. && 2 * R > interior.thickness) ?
                std::nan("") :
                Dst - interior.iso_surface;
}

inline double get_distance(const Vec3f &p, const Interior &interior)
{
    double d = get_distance_raw(p, interior) - interior.iso_surface;
    return d;
}

template<class T>
FloatingOnly<T> get_distance(const LegacyVec<3, T> &p, const Interior &interior)
{
    return get_distance(Vec3f(p.template cast<float>()), interior);
}

// A face that can be divided. Stores the indices into the original mesh if its
// part of that mesh and the vertices it consists of.
enum { NEW_FACE = -1};
struct DivFace {
    Index3 indx;
    std::array<Vec3f, 3> verts;
    long faceid = NEW_FACE;
    long parent = NEW_FACE;
};

// Divide a face recursively and call visitor on all the sub-faces.
template<class Fn>
void divide_triangle(const DivFace &face, Fn &&visitor)
{
    std::array<Vec3f, 3> edges = {(face.verts[0] - face.verts[1]),
                                  (face.verts[1] - face.verts[2]),
                                  (face.verts[2] - face.verts[0])};

    std::array<size_t, 3> edgeidx = {0, 1, 2};

    std::sort(edgeidx.begin(), edgeidx.end(), [&edges](size_t e1, size_t e2) {
        return edges[e1].squaredNorm() > edges[e2].squaredNorm();
    });

    DivFace child1, child2;

    child1.parent   = face.faceid == NEW_FACE ? face.parent : face.faceid;
    child1.indx[0]  = -1;
    child1.indx[1]  = face.indx[edgeidx[1]];
    child1.indx[2]  = face.indx[(edgeidx[1] + 1) % 3];
    child1.verts[0] = (face.verts[edgeidx[0]] + face.verts[(edgeidx[0] + 1) % 3]) / 2.;
    child1.verts[1] = face.verts[edgeidx[1]];
    child1.verts[2] = face.verts[(edgeidx[1] + 1) % 3];

    if (visitor(child1))
        divide_triangle(child1, std::forward<Fn>(visitor));

    child2.parent   = face.faceid == NEW_FACE ? face.parent : face.faceid;
    child2.indx[0]  = -1;
    child2.indx[1]  = face.indx[edgeidx[2]];
    child2.indx[2]  = face.indx[(edgeidx[2] + 1) % 3];
    child2.verts[0] = child1.verts[0];
    child2.verts[1] = face.verts[edgeidx[2]];
    child2.verts[2] = face.verts[(edgeidx[2] + 1) % 3];

    if (visitor(child2))
        divide_triangle(child2, std::forward<Fn>(visitor));
}

void remove_inside_triangles(indexed_triangle_set &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask)
{
    enum TrPos { posInside, posTouch, posOutside };

    auto &faces       = mesh.indices;
    auto &vertices    = mesh.vertices;
    auto bb           = Domain::bounding_box(mesh); //mesh.bounding_box();

    bool use_exclude_mask = faces.size() == exclude_mask.size();
    auto is_excluded = [&exclude_mask, use_exclude_mask](size_t face_id) {
        return use_exclude_mask && exclude_mask[face_id];
    };

    // TODO: Parallel mode not working yet
    constexpr auto &exec_policy = execution::ex_seq;

    // Info about the needed modifications on the input mesh.
    struct MeshMods {

        // Just a thread safe wrapper for a vector of triangles.
        struct {
            std::vector<std::array<Vec3f, 3>> data;
            execution::SpinningMutex<decltype(exec_policy)> mutex;

            void emplace_back(const std::array<Vec3f, 3> &pts)
            {
                std::lock_guard lk{mutex};
                data.emplace_back(pts);
            }

            size_t size() const { return data.size(); }
            const std::array<Vec3f, 3>& operator[](size_t idx) const
            {
                return data[idx];
            }

        } new_triangles;

        // A vector of bool for all faces signaling if it needs to be removed
        // or not.
        std::vector<bool> to_remove;

        MeshMods(const indexed_triangle_set &mesh):
            to_remove(mesh.indices.size(), false) {}

        // Number of triangles that need to be removed.
        size_t to_remove_cnt() const
        {
            return std::accumulate(to_remove.begin(), to_remove.end(), size_t(0));
        }

    } mesh_mods{mesh};

    // Must return true if further division of the face is needed.
    auto divfn = [&interior, bb, &mesh_mods](const DivFace &f) {
        const Domain::BoundingBox3f facebb_float{BB::construct(f.verts.begin(), f.verts.end())};
        Domain::BoundingBox3d facebb{BB::cast<double>(facebb_float)};

        // Face is certainly outside the cavity
        if (! facebb.overlap(bb) && f.faceid != NEW_FACE) {
            return false;
        }
        const auto facebb_radius{0.5 * (facebb.max - facebb.min).template cast<double>().norm()};
        TriangleBubble bubble{BB::center(facebb).cast<float>(), facebb_radius};

        double D = get_distance(bubble, interior);
        double R = bubble.R;

        if (std::isnan(D)) // The distance cannot be measured, triangle too big
            return true;

        // Distance of the bubble wall to the interior wall. Negative if the
        // bubble is overlapping with the interior
        double bubble_distance = D - R;

        // The face is crossing the interior or inside, it must be removed and
        // parts of it re-added, that are outside the interior
        if (bubble_distance < 0.) {
            if (f.faceid != NEW_FACE)
                mesh_mods.to_remove[f.faceid] = true;

            if (f.parent != NEW_FACE) // Top parent needs to be removed as well
                mesh_mods.to_remove[f.parent] = true;

            // If the outside part is between the interior and the exterior
            // (inside the wall being invisible), no further division is needed.
            if ((R + D) < interior.thickness)
                return false;

            return true;
        } else if (f.faceid == NEW_FACE) {
            // New face completely outside needs to be re-added.
            mesh_mods.new_triangles.emplace_back(f.verts);
        }

        return false;
    };

    interior.reset_accessor();

    execution::for_each(
        exec_policy, size_t(0), faces.size(),
        [&](size_t face_idx) {
            const Index3 &face = faces[face_idx];

            // If the triangle is excluded, we need to keep it.
            if (is_excluded(face_idx))
                return;

            std::array<Vec3f, 3> pts = {vertices[face[0]], vertices[face[1]],
                                        vertices[face[2]]};

            const Domain::BoundingBox3f facebb_float{BB::construct(pts.begin(), pts.end())};
            Domain::BoundingBox3d facebb{BB::cast<double>(facebb_float)};

            // Face is certainly outside the cavity
            if (!facebb.overlap(bb))
                return;

            DivFace df{face, pts, long(face_idx)};

            if (divfn(df)) divide_triangle(df, divfn);
        },
        execution::max_concurrency(exec_policy)
    );

    auto new_faces = reserve_vector<Index3>(faces.size() +
                                           mesh_mods.new_triangles.size());

    for (size_t face_idx = 0; face_idx < faces.size(); ++face_idx) {
        if (!mesh_mods.to_remove[face_idx])
            new_faces.emplace_back(faces[face_idx]);
    }

    for(size_t i = 0; i < mesh_mods.new_triangles.size(); ++i) {
        size_t o = vertices.size();
        vertices.emplace_back(mesh_mods.new_triangles[i][0]);
        vertices.emplace_back(mesh_mods.new_triangles[i][1]);
        vertices.emplace_back(mesh_mods.new_triangles[i][2]);
        new_faces.emplace_back(Index3{int(o), int(o + 1), int(o + 2)});
    }

    SPDLOG_INFO("Trimming: {} triangles removed", mesh_mods.to_remove_cnt());
    SPDLOG_INFO("Trimming: {} triangles added", mesh_mods.new_triangles.size());

    faces.swap(new_faces);
    new_faces = {};

//    mesh = TriangleMesh{mesh.its};
    //FIXME do we want to repair the mesh? Are there duplicate vertices or flipped triangles?
}

void remove_inside_triangles(TriangleMesh &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask)
{
    remove_inside_triangles(mesh.its, interior, exclude_mask);
}

struct FaceHash {

    // A 64 bit number's max hex digits
    static constexpr size_t MAX_NUM_CHARS = 16;

    // A hash is created for each triangle to be identifiable. The hash uses
    // only the triangle's geometric traits, not the index in a particular mesh.
    std::unordered_set<std::string> facehash;

    // Returns the string in reverse, but that is ok for hashing
    static std::array<char, MAX_NUM_CHARS + 1> to_chars(int64_t val)
    {
        std::array<char, MAX_NUM_CHARS + 1> ret;

        static const constexpr char * Conv = "0123456789abcdef";

        auto ptr = ret.begin();
        auto uval = static_cast<uint64_t>(std::abs(val));
        while (uval) {
            *ptr = Conv[uval & 0xf];
            ++ptr;
            uval = uval >> 4;
        }
        if (val < 0) { *ptr = '-'; ++ptr; }
        *ptr = '\0'; // C style string ending

        return ret;
    }

    static std::string hash(const LegacyVec<3, int64_t> &v)
    {
        std::string ret;
        ret.reserve(3 * MAX_NUM_CHARS);

        for (auto val : v)
            ret += to_chars(val).data();

        return ret;
    }

    static std::string facekey(const Index3 &face, const std::vector<Vec3f> &vertices)
    {
        // Scale to integer to avoid floating points
        std::array<LegacyVec<3, int64_t>, 3> pts = {
            scaled<int64_t>(vertices[face[0]]),
            scaled<int64_t>(vertices[face[1]]),
            scaled<int64_t>(vertices[face[2]])
        };

        // Get the first two sides of the triangle, do a cross product and move
        // that vector to the center of the triangle. This encodes all
        // information to identify an identical triangle at the same position.
        LegacyVec<3, int64_t> a = pts[0] - pts[2], b = pts[1] - pts[2];
        LegacyVec<3, int64_t> c = a.cross(b) + (pts[0] + pts[1] + pts[2]) / 3;

        // Return a concatenated string representation of the coordinates
        return hash(c);
    }

    FaceHash (const indexed_triangle_set &its): facehash(its.indices.size())
    {
        for (Index3 face : its.indices) {
            std::swap(face[0], face[2]);
            facehash.insert(facekey(face, its.vertices));
        }
    }

    bool find(const std::string &key)
    {
        auto it = facehash.find(key);
        return it != facehash.end();
    }
};


static void exclude_neighbors(const Index3                &face,
                              std::vector<bool>          &mask,
                              const indexed_triangle_set &its,
                              const VertexFaceIndex      &index,
                              size_t                      recursions)
{
    for (int i = 0; i < 3; ++i) {
        const auto &neighbors_range = index[face[i]];
        for (size_t fi_n : neighbors_range) {
            mask[fi_n] = true;
            if (recursions > 0)
                exclude_neighbors(its.indices[fi_n], mask, its, index, recursions - 1);
        }
    }
}

std::vector<bool> create_exclude_mask(const indexed_triangle_set   &its,
                                      const Interior               &interior,
                                      const std::vector<Domain::SLA::DrainHole> &holes)
{
    FaceHash interior_hash{sla::get_mesh(interior)};

    std::vector<bool> exclude_mask(its.indices.size(), false);

    VertexFaceIndex neighbor_index{its};

    for (size_t fi = 0; fi < its.indices.size(); ++fi) {
        auto &face = its.indices[fi];

        if (interior_hash.find(FaceHash::facekey(face, its.vertices))) {
            exclude_mask[fi] = true;
            continue;
        }

        if (exclude_mask[fi]) {
            exclude_neighbors(face, exclude_mask, its, neighbor_index, 1);
            continue;
        }

        // Lets deal with the holes. All the triangles of a hole and all the
        // neighbors of these triangles need to be kept. The neigbors were
        // created by CGAL mesh boolean operation that modified the original
        // interior inside the input mesh to contain the holes.
        Vec3d tr_center = (
                              its.vertices[face[0]] +
                              its.vertices[face[1]] +
                              its.vertices[face[2]]
                              ).cast<double>() / 3.;

        // If the center is more than half a mm inside the interior,
        // it cannot possibly be part of a hole wall.
        if (sla::get_distance(tr_center, interior) < -0.5)
            continue;

        Vec3f U = its.vertices[face[1]] - its.vertices[face[0]];
        Vec3f V = its.vertices[face[2]] - its.vertices[face[0]];
        Vec3f C = U.cross(V);
        Vec3f face_normal = C.normalized();

        for (const Domain::SLA::DrainHole &dh : holes) {
            if (dh.failed) continue;

            Vec3d dhpos = dh.pos.cast<double>();
            Vec3d dhend = dhpos + dh.normal.cast<double>() * dh.height;

            Linef3 holeaxis{dhpos, dhend};

            double D_hole_center = Biz::Algorithms::Line::line_alg::distance_to(holeaxis, tr_center);
            double D_hole        = std::abs(D_hole_center - dh.radius);
            float dot            = dh.normal.dot(face_normal);

            // Empiric tolerances for center distance and normals angle.
            // For triangles that are part of a hole wall the angle of
            // triangle normal and the hole axis is around 90 degrees,
            // so the dot product is around zero.
            double D_tol = dh.radius / Domain::SLA::DrainHole::steps;
            float normal_angle_tol = 1.f / Domain::SLA::DrainHole::steps;

            if (D_hole < D_tol && std::abs(dot) < normal_angle_tol) {
                exclude_mask[fi] = true;
                exclude_neighbors(face, exclude_mask, its, neighbor_index, 1);
            }
        }
    }

    return exclude_mask;
}

void transform_drainhole_points(Domain::SLA::DrainHoles& drain_holes, const Transform3d &trafo)
{
//    const Transform3d& vol_trafo = mo.volumes.front()->get_transformation().get_matrix();
    const Geometry::Transformation trans(trafo /** vol_trafo*/);
    const Transform3d& tr = trans.get_matrix();
    for (Domain::SLA::DrainHole& hl : drain_holes) {
        Vec3d pos = hl.pos.cast<double>();
        Vec3d nrm = hl.normal.cast<double>();

        pos = tr * pos;
        nrm = tr * nrm - tr.translation();

        // Now shift the hole a bit above the object and make it deeper to
        // compensate for it. This is to avoid problems when the hole is placed
        // on (nearly) flat surface.
        pos -= nrm.normalized() * sla::HoleStickOutLength;

        hl.pos = pos.cast<float>();
        hl.normal = nrm.cast<float>();
        hl.height += sla::HoleStickOutLength;
    }
}

double get_voxel_scale(double mesh_volume, const HollowingConfig &hc)
{
    static constexpr double MIN_SAMPLES_IN_WALL = 3.5;
    static constexpr double MAX_OVERSAMPL = 8.;
    static constexpr double UNIT_VOLUME   = 500000; // empiric

    // I can't figure out how to increase the grid resolution through openvdb
    // API so the model will be scaled up before conversion and the result
    // scaled down. Voxels have a unit size. If I set voxelSize smaller, it
    // scales the whole geometry down, and doesn't increase the number of
    // voxels.
    //
    // First an allowed range for voxel scale is determined from an initial
    // range of <MIN_SAMPLES_IN_WALL, MAX_OVERSAMPL>. The final voxel scale is
    // then chosen from this range using the 'quality:<0, 1>' parameter.
    // The minimum can be lowered if the wall thickness is great enough and
    // the maximum is lowered if the model volume very big.

    double sc_divider    = std::max(1.0, (mesh_volume / UNIT_VOLUME));
    double min_oversampl = std::max(MIN_SAMPLES_IN_WALL / hc.min_thickness, 1.);
    double max_oversampl_scaled = std::max(min_oversampl, MAX_OVERSAMPL / sc_divider);
    auto   voxel_scale          = min_oversampl + (max_oversampl_scaled - min_oversampl) * hc.quality;

    SPDLOG_DEBUG("Hollowing: max oversampl will be: {}", max_oversampl_scaled);
    SPDLOG_DEBUG("Hollowing: voxel scale will be: {}", voxel_scale);
    SPDLOG_DEBUG("Hollowing: mesh volume is: {}", mesh_volume);

    return voxel_scale;
}

// The same as its_compactify_vertices, but returns a new mesh, doesn't touch
// the original
static indexed_triangle_set
remove_unconnected_vertices(const indexed_triangle_set &its)
{
    if (its.indices.empty()) {};

    indexed_triangle_set M;

    std::vector<int> vtransl(its.vertices.size(), -1);
    int vcnt = 0;
    for (auto &f : its.indices) {

        for (int i = 0; i < 3; ++i)
            if (vtransl[size_t(f[i])] < 0) {

                M.vertices.emplace_back(its.vertices[size_t(f[i])]);
                vtransl[size_t(f[i])] = vcnt++;
            }

        std::array<int, 3> new_f = {
            vtransl[size_t(f[0])],
            vtransl[size_t(f[1])],
            vtransl[size_t(f[2])]
        };

        M.indices.emplace_back(Index3{new_f[0], new_f[1], new_f[2]});
    }

    return M;
}

int hollow_mesh_and_drill(indexed_triangle_set &hollowed_mesh,
                           const Interior &interior,
                           const Domain::SLA::DrainHoles &drainholes,
                           std::function<void(size_t)> on_hole_fail)
{
    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        hollowed_mesh.vertices,
        hollowed_mesh.indices
        );

    std::uniform_real_distribution<float> dist(0., float(EPSILON));
    auto holes_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal({}, {});
    indexed_triangle_set part_to_drill = hollowed_mesh;

    std::mt19937 m_rng{std::random_device{}()};

    for (size_t i = 0; i < drainholes.size(); ++i) {
        Domain::SLA::DrainHole holept = drainholes[i];

        holept.normal += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        holept.normal.normalize();
        holept.pos += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        indexed_triangle_set m = to_mesh(holept);

        part_to_drill.indices.clear();
        auto bb = Domain::bounding_box(m);
        Eigen::AlignedBox<float, 3> ebb{bb.min.cast<float>(),
                                        bb.max.cast<float>()};

        AABBTreeIndirect::traverse(
            tree,
            AABBTreeIndirect::intersecting(ebb),
            [&part_to_drill, &hollowed_mesh](const auto& node)
            {
                part_to_drill.indices.emplace_back(hollowed_mesh.indices[node.idx]);
                // continue traversal
                return true;
            });

        auto cgal_meshpart = MeshBoolean::cgal::triangle_mesh_to_cgal(
            remove_unconnected_vertices(part_to_drill));

        if (MeshBoolean::cgal::does_self_intersect(*cgal_meshpart)) {
            on_hole_fail(i);
            continue;
        }

        auto cgal_hole = MeshBoolean::cgal::triangle_mesh_to_cgal(m);
        MeshBoolean::cgal::plus(*holes_mesh_cgal, *cgal_hole);
    }

    auto ret = static_cast<int>(HollowMeshResult::Ok);

    if (MeshBoolean::cgal::does_self_intersect(*holes_mesh_cgal)) {
        ret |= static_cast<int>(HollowMeshResult::DrillingFailed);
    }

    auto hollowed_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal(hollowed_mesh);

    if (!MeshBoolean::cgal::does_bound_a_volume(*hollowed_mesh_cgal)) {
        ret |= static_cast<int>(HollowMeshResult::FaultyMesh);
    }

    if (!MeshBoolean::cgal::empty(*holes_mesh_cgal)
        && !MeshBoolean::cgal::does_bound_a_volume(*holes_mesh_cgal)) {
        ret |= static_cast<int>(HollowMeshResult::FaultyHoles);
    }

    // Don't even bother
    if (ret & static_cast<int>(HollowMeshResult::DrillingFailed))
        return ret;

    try {
        if (!MeshBoolean::cgal::empty(*holes_mesh_cgal))
            MeshBoolean::cgal::minus(*hollowed_mesh_cgal, *holes_mesh_cgal);

        hollowed_mesh =
            MeshBoolean::cgal::cgal_to_indexed_triangle_set(*hollowed_mesh_cgal);

        std::vector<bool> exclude_mask =
            create_exclude_mask(hollowed_mesh, interior, drainholes);

        sla::remove_inside_triangles(hollowed_mesh, interior, exclude_mask);
    } catch (const Slic3r::RuntimeError &) {
        ret |= static_cast<int>(HollowMeshResult::DrillingFailed);
    }

    return ret;
}

}} // namespace Slic3r::sla
