#include "Slic3r/Biz/Utils/MeshClipper.hpp"

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"

#include "libslic3r/TriangleMeshSlicer.hpp"

namespace Slic3r::Biz {

using namespace Domain;
using namespace Algorithms;

void MeshClipper::set_behaviour(bool fill_cut, double contour_width)
{
    if (fill_cut != m_fill_cut || !is_approx(contour_width, m_contour_width))
        result.reset();
    m_fill_cut      = fill_cut;
    m_contour_width = contour_width;
}

void MeshClipper::set_plane(const ClippingPlane& plane)
{
    if (m_plane != plane) {
        m_plane = plane;
        result.reset();
    }
}

void MeshClipper::set_limiting_plane(const ClippingPlane& plane)
{
    if (m_limiting_plane != plane) {
        m_limiting_plane = plane;
        result.reset();
    }
}

void MeshClipper::set_mesh(const indexed_triangle_set& mesh)
{
    if (m_mesh != &mesh) {
        m_mesh = &mesh;
        result.reset();
    }
}

void MeshClipper::set_negative_mesh(const indexed_triangle_set& mesh)
{
    if (m_negative_mesh != &mesh) {
        m_negative_mesh = &mesh;
        result.reset();
    }
}

void MeshClipper::set_transformation(const Transformation& trafo)
{
    if (!m_trafo.get_matrix().isApprox(trafo.get_matrix())) {
        m_trafo = trafo;
        result.reset();
    }
}

size_t MeshClipper::get_contour_id_from_projection(const Vec3d& point_in) const
{
    if (!result || result->cut_islands.empty())
        return -1;
    Vec3d point = result->trafo.inverse() * point_in;
    Point pt_2d = Scaling::scaled(Vec2d(point.x(), point.y()));

    for (size_t i = 0; i < result->cut_islands.size(); ++i) {
        const CutIsland& isl = result->cut_islands[i];
        if (isl.expoly_bb.contains(pt_2d) && ExPolygon::contains(isl.expoly, pt_2d))
            return i; // TODO: handle intersecting contours
    }
    return size_t (- 1);
}

bool MeshClipper::has_valid_contour() const
{
    return result
        && std::any_of(
               result->cut_islands.begin(),
               result->cut_islands.end(),
               [](const CutIsland& isl) { return !isl.expoly.empty(); }
        );
}

std::vector<Vec3d> MeshClipper::point_per_contour() const
{
    assert(result);
    std::vector<Vec3d> out;

    for (const CutIsland& isl : result->cut_islands) {
        assert(isl.expoly.contour.size() > 2);
        // Now return a point lying inside the contour but not in a hole.
        // We do this by taking a point lying close to the edge, repeating
        // this several times for different edges and distances from them.
        // (We prefer point not extremely close to the border.
        bool done = false;
        Vec2d p;
        size_t i = 1;
        while (i < isl.expoly.contour.size()) {
            const Vec2d a = Scaling::unscaled<double>(isl.expoly.contour.points[i - 1]);
            const Vec2d b = Scaling::unscaled<double>(isl.expoly.contour.points[i]);
            Vec2d n        = (b - a).normalized();
            std::swap(n.x(), n.y());
            n.x()    = -1 * n.x();
            double f = 10.;
            while (f > 0.05) {
                p = (0.5 * (b + a)) + f * n;
                if (ExPolygon::contains(isl.expoly, Scaling::scaled(p))) {
                    done = true;
                    break;
                }
                f = f / 10.;
            }
            if (done)
                break;
            i += std::max(size_t(2), isl.expoly.contour.size() / 5);
        }
        // If the above failed, just return the centroid, regardless of whether
        // it is inside the contour or in a hole (we must return something).
        Vec2d c = done ? p : Scaling::unscaled<double>(isl.expoly.contour.centroid());
        out.emplace_back(result->trafo * Vec3d(c.x(), c.y(), 0.));
    }
    return out;
}

void MeshClipper::update_result()
{
    if (!result)
        recalculate_triangles();
}

struct InitDataGeometry
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    void reserve_vertices(size_t vertices_count) { vertices.reserve(vertices_count * 6); }
    void reserve_indices(size_t indices_count) { indices.reserve(indices_count); }


    void add_vertex(const Vec3f& position, const Vec3f& normal) {
        vertices.insert(vertices.end(), position.data(), position.data() + 3);
        vertices.insert(vertices.end(), normal.data(), normal.data() + 3);
    }
    void add_triangle(unsigned int id1, unsigned int id2, unsigned int id3) {
        indices.emplace_back(id1);
        indices.emplace_back(id2);
        indices.emplace_back(id3);
    }

    bool is_empty() const { return vertices_count() == 0 || indices_count() == 0; }

    size_t vertices_count() const { return vertices.size() / 6; }
    size_t indices_count() const { return indices.size(); }

    Vec3f extract_position_3(size_t id) const
    {
        if (vertices_count() <= id) {
            assert(false);
            return { FLT_MAX, FLT_MAX, FLT_MAX };
        }

        const float* start = &vertices[id * 6];
        return { *(start + 0), *(start + 1), *(start + 2) };
    }

    int extract_index(size_t id) const
    {
        if (indices_count() <= id) {
            assert(false);
            return -1;
        }

        return indices[id];
    }

    indexed_triangle_set get_as_indexed_triangle_set() const {

        indexed_triangle_set its;
        its.vertices.reserve(vertices_count());
        for (size_t i = 0; i < vertices_count(); ++i) {
            its.vertices.emplace_back(extract_position_3(i));
        }
        its.indices.reserve(indices_count() / 3);
        for (size_t i = 0; i < indices_count() / 3; ++i) {
            const size_t tri_id = i * 3;
            its.indices.emplace_back(stl_triangle_vertex_indices{ extract_index(tri_id), extract_index(tri_id + 1), extract_index(tri_id + 2) });
        }
        return its;
    }
};

void MeshClipper::recalculate_triangles()
{
    result = ClipResult();

    auto plane_mesh =
        Eigen::Hyperplane<double, 3>(m_plane.get_normal(), -m_plane.distance(Vec3d::Zero()))
            .transform(m_trafo.get_matrix().inverse());
    const Vec3d up          = plane_mesh.normal();
    const float height_mesh = -plane_mesh.offset();

    // Now do the cutting
    MeshSlicingParams slicing_params;
    slicing_params.trafo.rotate(
        Eigen::Quaternion<double, Eigen::DontAlign>::FromTwoVectors(up, Vec3d::UnitZ())
    );

    ExPolygons expolys;

    // if (m_csgmesh.empty()) {
    if (m_mesh)
        expolys = ClipperUtils::union_ex(slice_mesh(*m_mesh, height_mesh, slicing_params));

    if (m_negative_mesh && !m_negative_mesh->empty()) {
        const ExPolygons neg_expolys =
            ClipperUtils::union_ex(slice_mesh(*m_negative_mesh, height_mesh, slicing_params));
        expolys = ClipperUtils::diff_ex(expolys, neg_expolys);
    }
    //} else {
    // expolys = std::move(
    // csg::slice_csgmesh_ex(
    // range(m_csgmesh),
    // {height_mesh},
    // MeshSlicingParamsEx{slicing_params}
    // )
    // .front()
    // );
    //}

    // Triangulate and rotate the cut into world coords:
    Eigen::Quaterniond q;
    q.setFromTwoVectors(Vec3d::UnitZ(), up);
    Transform3d tr = Transform3d::Identity();
    tr.rotate(q);
    tr = m_trafo.get_matrix() * tr;

    result->trafo = tr;

    if (m_limiting_plane != ClippingPlane::ClipsNothing()) {
        // Now remove whatever ended up below the limiting plane (e.g. sinking objects).
        // First transform the limiting plane from world to mesh coords.
        // Note that inverse of tr transforms the plane from world to horizontal.
        const Vec3d normal_old = m_limiting_plane.get_normal().normalized();
        const Vec3d normal_new =
            (tr.matrix().block<3, 3>(0, 0).transpose() * normal_old).normalized();

        // normal_new should now be the plane normal in mesh coords. To find the offset,
        // transform a point and set offset so it belongs to the transformed plane.
        Vec3d pt                  = Vec3d::Zero();
        const double plane_offset = m_limiting_plane.get_data()[3];
        if (std::abs(normal_old.z())
            > 0.5) // normal is normalized, at least one of the coords if larger than sqrt(3)/3 = 0.57
            pt.z() = -plane_offset / normal_old.z();
        else if (std::abs(normal_old.y()) > 0.5)
            pt.y() = -plane_offset / normal_old.y();
        else
            pt.x() = -plane_offset / normal_old.x();
        pt                  = tr.inverse() * pt;
        const double offset = -(normal_new.dot(pt));

        if (std::abs(normal_old.dot(m_plane.get_normal().normalized())) > 0.99) {
            // The cuts are parallel, show all or nothing.
            if (normal_old.dot(m_plane.get_normal().normalized()) < 0.0 && offset < height_mesh)
                expolys.clear();
        } else {
            // The cut is a horizontal plane defined by z=height_mesh.
            // ax+by+e=0 is the line of intersection with the limiting plane.
            // Normalized so a^2 + b^2 = 1.
            const double len = std::hypot(normal_new.x(), normal_new.y());
            if (len == 0.)
                return;
            const double a = normal_new.x() / len;
            const double b = normal_new.y() / len;
            const double e = (normal_new.z() * height_mesh + offset) / len;

            // We need a half-plane to limit the cut. Get angle of the intersecting line.
            double angle = (b != 0.0) ? std::atan(-a / b) : ((a < 0.0) ? -0.5 * M_PI : 0.5 * M_PI);
            if (b > 0) // select correct half-plane
                angle += M_PI;

            // We'll take a big rectangle above x-axis and rotate and translate
            // it so it lies on our line. This will be the figure to subtract
            // from the cut. The coordinates must not overflow after the transform,
            // make the rectangle a bit smaller.
            const coord_t size = (std::numeric_limits<coord_t>::max() / 2
                                  - scale_(std::max(std::abs(e * a), std::abs(e * b))))
                / 4;
            Polygons ep{Polygon(
                {Point(-size, 0), Point(size, 0), Point(size, 2 * size), Point(-size, 2 * size)}
            )};
            ep.front().rotate(angle);
            ep.front().translate(scale_(-e * a), scale_(-e * b));
            expolys = ClipperUtils::diff_ex(expolys, ep);
        }
    }

    tr.pretranslate(0.001 * m_plane.get_normal().normalized()); // to avoid z-fighting
    Transform3d tr2 = tr;
    tr2.pretranslate(0.002 * m_plane.get_normal().normalized());

    std::vector<Vec2f> triangles2d;

    for (const Domain::ExPolygon& exp : expolys) {
        triangles2d.clear();

        result->cut_islands.push_back(CutIsland());
        CutIsland& isl = result->cut_islands.back();

        if (m_fill_cut) {
            triangles2d = Tesselate::triangulate_expolygon_2f(
                exp,
                m_trafo.get_matrix().matrix().determinant() < 0.
            );
            InitDataGeometry init_data;
            init_data.reserve_vertices(triangles2d.size());
            init_data.reserve_indices(triangles2d.size());

            // vertices + indices
            for (auto it = triangles2d.cbegin(); it != triangles2d.cend(); it = it + 3) {
                init_data.add_vertex(
                    (Vec3f) (tr * Vec3d((*(it + 0)).x(), (*(it + 0)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                init_data.add_vertex(
                    (Vec3f) (tr * Vec3d((*(it + 1)).x(), (*(it + 1)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                init_data.add_vertex(
                    (Vec3f) (tr * Vec3d((*(it + 2)).x(), (*(it + 2)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                const size_t idx = it - triangles2d.cbegin();
                init_data.add_triangle(
                    (unsigned int) idx,
                    (unsigned int) idx + 1,
                    (unsigned int) idx + 2
                );
            }

            if (!init_data.is_empty())
                isl.model = init_data.get_as_indexed_triangle_set();
        }

        if (m_contour_width != 0. && !exp.contour.empty()) {
            triangles2d.clear();

            // The contours must not scale with the object. Check the scale factor
            // in the respective directions, create a scaled copy of the ExPolygon
            // offset it and then unscale the result again.

            Transform3d t   = tr;
            t.translation() = Vec3d::Zero();
            double scale_x  = (t * Vec3d::UnitX()).norm();
            double scale_y  = (t * Vec3d::UnitY()).norm();

            // To prevent overflow after scaling, downscale the input if needed:
            double extra_scale = 1.;
            int32_t limit      = int32_t(
                std::min(
                    std::numeric_limits<coord_t>::max() / (2. * std::max(1., scale_x)),
                    std::numeric_limits<coord_t>::max() / (2. * std::max(1., scale_y))
                )
            );
            int32_t max_coord = 0;
            for (const Point& pt : exp.contour)
                max_coord = std::max(max_coord, std::max(std::abs(pt.x()), std::abs(pt.y())));
            if (max_coord + m_contour_width >= limit)
                extra_scale = 0.9 * double(limit) / max_coord;

            Domain::ExPolygon exp_copy = exp;
            if (extra_scale != 1.)
                exp_copy.scale(extra_scale);
            exp_copy.scale(scale_x, scale_y);

            ExPolygons expolys_exp = ClipperUtils::offset_ex(exp_copy, scale_(m_contour_width));
            expolys_exp            = ClipperUtils::diff_ex(expolys_exp, ExPolygons({exp_copy}));

            for (Domain::ExPolygon& e : expolys_exp) {
                e.scale(1. / scale_x, 1. / scale_y);
                if (extra_scale != 1.)
                    e.scale(1. / extra_scale);
            }

            triangles2d = Tesselate::triangulate_expolygons_2f(
                expolys_exp,
                m_trafo.get_matrix().matrix().determinant() < 0.
            );
            InitDataGeometry init_data;
            init_data.reserve_vertices(triangles2d.size());
            init_data.reserve_indices(triangles2d.size());

            // vertices + indices
            for (auto it = triangles2d.cbegin(); it != triangles2d.cend(); it = it + 3) {
                init_data.add_vertex(
                    (Vec3f) (tr2 * Vec3d((*(it + 0)).x(), (*(it + 0)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                init_data.add_vertex(
                    (Vec3f) (tr2 * Vec3d((*(it + 1)).x(), (*(it + 1)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                init_data.add_vertex(
                    (Vec3f) (tr2 * Vec3d((*(it + 2)).x(), (*(it + 2)).y(), height_mesh))
                        .cast<float>(),
                    (Vec3f) up.cast<float>()
                );
                const size_t idx = it - triangles2d.cbegin();
                init_data.add_triangle(
                    (unsigned short) idx,
                    (unsigned short) idx + 1,
                    (unsigned short) idx + 2
                );
            }

            if (!init_data.is_empty())
                isl.model_expanded = init_data.get_as_indexed_triangle_set();
        }

        isl.expoly    = std::move(exp);
        isl.expoly_bb = Algorithms::ExPolygon::get_extents(isl.expoly);

        Point centroid_scaled = isl.expoly.contour.centroid();
        Vec3d centroid_world  = result->trafo
            * Vec3d(Scaling::unscaled<double>(centroid_scaled).x(),
                    Scaling::unscaled<double>(centroid_scaled).y(),
                    0.);
        isl.hash = isl.expoly.contour.size()
            + size_t(std::abs(100. * centroid_world.x()))
            + size_t(std::abs(100. * centroid_world.y()))
            + size_t(std::abs(100. * centroid_world.z()));
    }

    // Now sort the islands so they are in defined order. This is a hack needed by cut gizmo, which sometimes
    // flips the normal of the cut, in which case the contours stay the same but their order may change.
    std::sort(
        result->cut_islands.begin(),
        result->cut_islands.end(),
        [](const CutIsland& a, const CutIsland& b) { return a.hash < b.hash; }
    );
}

} // namespace Slic3r::Biz
