///|/ Copyright (c) Prusa Research 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
///|/ Convex-hull face planes adapted from GLGizmoFlatten::update_planes().
///|/
#include "GLGizmoFaceAlignTypes.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>

namespace Slic3r::GUI {

namespace {

struct PlaneWork {
    std::vector<Vec3d> vertices;
    Vec3d              normal;
    float              area{ 0.f };
    Vec3d              center_world;
    Vec3d              normal_world;
    PickingModel       vbo;
};

} // namespace

void face_align_build_planes_for_instance(
    const ModelObject*               mo,
    int                              instance_idx,
    int                              object_idx_tag,
    const Transform3d&               mesh_to_world,
    std::vector<FaceAlignPlaneData>& out_planes,
    int                              max_planes)
{
    if (mo == nullptr || instance_idx < 0 || instance_idx >= (int)mo->instances.size())
        return;

    TriangleMesh ch;
    for (const ModelVolume* vol : mo->volumes) {
        if (vol->type() != ModelVolumeType::MODEL_PART)
            continue;
        TriangleMesh vol_ch = vol->get_convex_hull();
        vol_ch.transform(vol->get_matrix());
        ch.merge(vol_ch);
    }
    ch = ch.convex_hull_3d();

    const Transform3d inst_matrix = mo->instances[instance_idx]->get_matrix_no_offset();

    const float minimal_area = 5.f;
    const float minimal_side = 1.f;

    std::vector<PlaneWork> work_planes;
    const int               num_of_facets  = ch.facets_count();
    const std::vector<Vec3f> face_normals   = its_face_normals(ch.its);
    const std::vector<Vec3i> face_neighbors = its_face_neighbors(ch.its);
    std::vector<int>         facet_queue(num_of_facets, 0);
    std::vector<bool>        facet_visited(num_of_facets, false);
    int                      facet_queue_cnt = 0;
    const stl_normal*        normal_ptr = nullptr;
    int                      facet_idx = 0;

    while (true) {
        for (; facet_idx < num_of_facets; ++facet_idx)
            if (!facet_visited[facet_idx]) {
                facet_queue[facet_queue_cnt++] = facet_idx;
                facet_visited[facet_idx]       = true;
                normal_ptr                     = &face_normals[facet_idx];
                work_planes.emplace_back();
                break;
            }
        if (facet_idx == num_of_facets)
            break;

        while (facet_queue_cnt > 0) {
            int fidx = facet_queue[--facet_queue_cnt];
            const stl_normal& this_normal = face_normals[fidx];
            if (std::abs(this_normal(0) - (*normal_ptr)(0)) < 0.001 && std::abs(this_normal(1) - (*normal_ptr)(1)) < 0.001 &&
                std::abs(this_normal(2) - (*normal_ptr)(2)) < 0.001) {
                const Vec3i face = ch.its.indices[fidx];
                for (int j = 0; j < 3; ++j)
                    work_planes.back().vertices.emplace_back(ch.its.vertices[face[j]].cast<double>());

                facet_visited[fidx] = true;
                for (int j = 0; j < 3; ++j)
                    if (int neighbor_idx = face_neighbors[fidx][j]; neighbor_idx >= 0 && !facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt++] = neighbor_idx;
            }
        }
        work_planes.back().normal = normal_ptr->cast<double>();

        Pointf3s& verts = work_planes.back().vertices;
        verts           = transform(verts, inst_matrix);

        if (verts.size() == 3 && ((verts[0] - verts[1]).norm() < minimal_side || (verts[0] - verts[2]).norm() < minimal_side ||
                                  (verts[1] - verts[2]).norm() < minimal_side))
            work_planes.pop_back();
    }

    const Matrix3d normal_matrix = inst_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();

    for (auto& pl : work_planes)
        pl.normal_world = (normal_matrix * pl.normal).normalized();

    for (unsigned int polygon_id = 0; polygon_id < work_planes.size(); ++polygon_id) {
        Pointf3s&       polygon = work_planes[polygon_id].vertices;
        const Vec3d&    normal  = work_planes[polygon_id].normal;
        const Vec3d     normal_transformed = normal_matrix * normal;

        Eigen::Quaterniond q;
        Transform3d        m = Transform3d::Identity();
        m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(normal_transformed, Vec3d::UnitZ()).toRotationMatrix();
        polygon                        = transform(polygon, m);

        Vec3d bb_size = BoundingBoxf3(polygon).size();
        float sf      = std::min(1. / bb_size(0), 1. / bb_size(1));
        Transform3d tr = Geometry::scale_transform({ sf, sf, 1.f });
        polygon        = transform(polygon, tr);
        polygon        = Slic3r::Geometry::convex_hull(polygon);
        polygon        = transform(polygon, tr.inverse());

        float& area = work_planes[polygon_id].area;
        area        = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++)
            area += polygon[i](0) * polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0) * polygon[i](1);
        area = 0.5f * std::abs(area);

        bool discard = false;
        if (area < minimal_area)
            discard = true;
        else {
            const double angle_threshold = ::cos(10.0 * (double)PI / 180.0);
            for (unsigned int i = 0; i < polygon.size(); ++i) {
                const Vec3d& prec = polygon[(i == 0) ? polygon.size() - 1 : i - 1];
                const Vec3d& curr = polygon[i];
                const Vec3d& next = polygon[(i == polygon.size() - 1) ? 0 : i + 1];
                if ((prec - curr).normalized().dot((next - curr).normalized()) > angle_threshold) {
                    discard = true;
                    break;
                }
            }
        }

        if (discard) {
            work_planes[polygon_id--] = std::move(work_planes.back());
            work_planes.pop_back();
            continue;
        }

        Vec3d centroid = Vec3d::Zero();
        for (const Vec3d& v : polygon)
            centroid += v;
        centroid /= (double)polygon.size();
        for (auto& vertex : polygon)
            vertex = 0.9f * vertex + 0.1f * centroid;

        const unsigned int k           = 10;
        const float        aggressivity = 0.2f;
        const unsigned int N            = (unsigned int)polygon.size();
        std::vector<std::pair<unsigned int, unsigned int>> neighbours;
        if (k != 0) {
            Pointf3s points_out(2 * k * N);
            for (unsigned int j = 0; j < N; ++j) {
                points_out[j * 2 * k] = polygon[j];
                neighbours.push_back(std::make_pair((int)(j * 2 * k - k) < 0 ? (N - 1) * 2 * k + k : j * 2 * k - k, j * 2 * k + k));
            }

            for (unsigned int i = 0; i < k; ++i) {
                for (unsigned int j = 0; j < N; ++j)
                    if (i == 0)
                        points_out[j * 2 * k + k] = 0.5f * (points_out[j * 2 * k] + points_out[j == N - 1 ? 0 : (j + 1) * 2 * k]);
                    else {
                        float r = 0.2f + 0.3f / (k - 1) * i;
                        points_out[neighbours[j].first]  = r * points_out[j * 2 * k] + (1 - r) * points_out[neighbours[j].first - 1];
                        points_out[neighbours[j].second] = r * points_out[j * 2 * k] + (1 - r) * points_out[neighbours[j].second + 1];
                    }
                for (unsigned int j = 0; j < N; ++j)
                    points_out[2 * k * j] = (1 - aggressivity) * points_out[2 * k * j] +
                                            aggressivity * 0.5f * (points_out[neighbours[j].first] + points_out[neighbours[j].second]);

                for (auto& n : neighbours) {
                    ++n.first;
                    --n.second;
                }
            }
            polygon = points_out;
        }

        for (auto& b : polygon)
            b(2) += 0.1f;

        polygon = transform(polygon, inst_matrix.inverse() * m.inverse());

        Vec3d sum_w = Vec3d::Zero();
        for (const Vec3d& v : polygon)
            sum_w += mesh_to_world * v;
        work_planes[polygon_id].center_world = sum_w / double(polygon.size());
    }

    std::sort(work_planes.rbegin(), work_planes.rend(), [](const PlaneWork& a, const PlaneWork& b) { return a.area < b.area; });
    work_planes.resize(std::min((int)work_planes.size(), max_planes));

    for (auto& plane : work_planes) {
        indexed_triangle_set its;
        its.vertices.reserve(plane.vertices.size());
        its.indices.reserve(plane.vertices.size() / 3);
        for (size_t i = 0; i < plane.vertices.size(); ++i)
            its.vertices.emplace_back((Vec3f)plane.vertices[i].cast<float>());
        for (size_t i = 1; i < plane.vertices.size() - 1; ++i)
            its.indices.emplace_back(0, i, i + 1);

        plane.vbo.model.init_from(its);
        if (Geometry::Transformation(inst_matrix).is_left_handed()) {
            for (stl_triangle_vertex_indices& face : its.indices) {
                if (its_face_normal(its, face).cast<double>().dot(plane.normal) < 0.0)
                    std::swap(face[1], face[2]);
            }
        }
        plane.vbo.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));

        FaceAlignPlaneData out;
        out.vbo            = std::move(plane.vbo);
        out.normal_world   = plane.normal_world;
        out.center_world   = plane.center_world;
        out.object_idx     = object_idx_tag;
        out.instance_idx   = instance_idx;
        out_planes.push_back(std::move(out));
    }
}

} // namespace Slic3r::GUI
