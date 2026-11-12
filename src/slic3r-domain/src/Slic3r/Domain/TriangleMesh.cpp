#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Utils.hpp"

namespace Slic3r::Domain {

void update_bounding_box(const indexed_triangle_set& its, TriangleMeshStats& out)
{
    BoundingBox3d bbox = bounding_box(its);
    out.min            = bbox.min.cast<float>();
    out.max            = bbox.max.cast<float>();
    out.size           = out.max - out.min;
}

TriangleMesh::TriangleMesh(indexed_triangle_set&& its) : its{std::move(its)} {}

TriangleMesh::TriangleMesh(indexed_triangle_set&& its, TriangleMeshStats&& stats) :
    its{std::move(its)},
    m_stats{std::move(stats)}
{}

TriangleMesh::TriangleMesh(const indexed_triangle_set& its, const TriangleMeshStats& stats) :
    its{its},
    m_stats{stats}
{}

float TriangleMesh::volume()
{
    if (m_stats.volume == -1)
        m_stats.volume = its_volume(this->its);
    return m_stats.volume;
}

void TriangleMesh::scale(float factor)
{
    this->scale(Vec3f(factor, factor, factor));
}

void TriangleMesh::scale(const Vec3f& versor)
{
    // Scale extents.
    auto s = versor.array();
    m_stats.min.array() *= s;
    m_stats.max.array() *= s;
    // Scale size.
    m_stats.size.array() *= s;
    // Scale volume.
    if (m_stats.volume > 0.0)
        m_stats.volume *= s(0) * s(1) * s(2);
    if (versor.x() == versor.y() && versor.x() == versor.z()) {
        float s = versor.x();
        for (stl_vertex& v : this->its.vertices)
            v *= s;
    } else {
        for (stl_vertex& v : this->its.vertices) {
            v.x() *= versor.x();
            v.y() *= versor.y();
            v.z() *= versor.z();
        }
    }
}

void TriangleMesh::translate(const Vec3f& displacement)
{
    if (displacement.x() != 0.f || displacement.y() != 0.f || displacement.z() != 0.f) {
        for (stl_vertex& v : this->its.vertices)
            v += displacement;
        m_stats.min += displacement;
        m_stats.max += displacement;
    }
}

void TriangleMesh::rotate(float angle, const Axis& axis)
{
    if (angle != 0.f) {
        angle = rad2deg(angle);
        switch (axis) {
        case Axis::X:
            its_rotate_x(this->its, angle);
            break;
        case Axis::Y:
            its_rotate_y(this->its, angle);
            break;
        case Axis::Z:
            its_rotate_z(this->its, angle);
            break;
        default:
            assert(false);
            return;
        }
        update_bounding_box(this->its, m_stats);
    }
}

void TriangleMesh::mirror(const Axis axis)
{
    switch (axis) {
    case Axis::X:
        for (stl_vertex& v : its.vertices)
            v.x() *= -1.f;
        break;
    case Axis::Y:
        for (stl_vertex& v : this->its.vertices)
            v.y() *= -1.0;
        break;
    case Axis::Z:
        for (stl_vertex& v : this->its.vertices)
            v.z() *= -1.0;
        break;
    default:
        assert(false);
        return;
    };
    its_flip_triangles(this->its);
    int iaxis = int(axis);
    std::swap(m_stats.min[iaxis], m_stats.max[iaxis]);
    m_stats.min[iaxis] *= -1.0;
    m_stats.max[iaxis] *= -1.0;
}

void TriangleMesh::transform(const Transform3d& t, bool fix_left_handed)
{
    its_transform(its, t);
    double det = t.matrix().block(0, 0, 3, 3).determinant();
    if (fix_left_handed && det < 0.) {
        its_flip_triangles(its);
        det = -det;
    }
    m_stats.volume *= det;
    update_bounding_box(this->its, m_stats);
}

void TriangleMesh::flip_triangles()
{
    its_flip_triangles(its);
    m_stats.volume = -m_stats.volume;
}

void TriangleMesh::align_to_origin()
{
    this->translate(-m_stats.min);
}

void TriangleMesh::rotate(double angle, Point* center)
{
    if (angle != 0.) {
        Vec2f c = center->cast<float>();
        this->translate(Vec3f{-c(0), -c(1), 0});
        its_rotate_z(this->its, (float) angle);
        this->translate(Vec3f{c(0), c(1), 0});
    }
}

void TriangleMesh::merge(const TriangleMesh& mesh)
{
    its_merge(this->its, mesh.its);
    m_stats = m_stats.merge(mesh.m_stats);
}

BoundingBox3d TriangleMesh::bounding_box() const
{
    BoundingBox3d bb;
    bb.defined = true;
    bb.min     = m_stats.min.cast<double>();
    bb.max     = m_stats.max.cast<double>();
    return bb;
}

bool TriangleMesh::has_zero_volume() const
{
    const Vec3d sz          = size();
    const double volume_val = sz.x() * sz.y() * sz.z();

    return Domain::is_approx(volume_val, 0., 0.1);
}

size_t TriangleMesh::memsize() const
{
    size_t memsize = 8 + this->its.memsize() + sizeof(m_stats);
    return memsize;
}

float its_volume(const indexed_triangle_set& its)
{
    if (its.empty())
        return 0.;

    // Choose a point, any point as the reference.
    auto p0      = its.vertices.front();
    float volume = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++i) {
        // Do dot product to get distance from point to plane.
        its_triangle triangle = its_triangle_vertices(its, i);
        Vec3f U               = triangle[1] - triangle[0];
        Vec3f V               = triangle[2] - triangle[0];
        Vec3f C               = U.cross(V);
        Vec3f normal          = C.normalized();
        float area            = 0.5 * C.norm();
        float height          = normal.dot(triangle[0] - p0);
        volume += (area * height) / 3.0f;
    }

    return volume;
}

void its_flip_triangles(indexed_triangle_set& its)
{
    for (stl_triangle_vertex_indices& face : its.indices)
        std::swap(face[1], face[2]);
}

void its_merge(indexed_triangle_set& its, indexed_triangle_set&& its_add)
{
    if (its.empty()) {
        its = std::move(its_add);
        return;
    }
    auto& verts       = its.vertices;
    size_t verts_size = verts.size();
    Slic3r::append(verts, std::move(its_add.vertices));

    // increase face indices
    int offset = static_cast<int>(verts_size);
    for (auto& face : its_add.indices)
        for (int i = 0; i < 3; ++i)
            face[i] += offset;
    Slic3r::append(its.indices, std::move(its_add.indices));
}

void its_merge(indexed_triangle_set& A, const indexed_triangle_set& B)
{
    auto N   = int(A.vertices.size());
    auto N_f = A.indices.size();

    A.vertices.insert(A.vertices.end(), B.vertices.begin(), B.vertices.end());
    A.indices.insert(A.indices.end(), B.indices.begin(), B.indices.end());

    for (size_t n = N_f; n < A.indices.size(); n++) {
        A.indices[n][0] += N;
        A.indices[n][1] += N;
        A.indices[n][2] += N;
    }
}

void its_merge(indexed_triangle_set& A, const std::vector<Vec3f>& triangles)
{
    const size_t offs = A.vertices.size();
    A.vertices.insert(A.vertices.end(), triangles.begin(), triangles.end());
    A.indices.reserve(A.indices.size() + A.vertices.size() / 3);

    for (int i = int(offs); i < int(A.vertices.size()); i += 3)
        A.indices.emplace_back(Index3{i, i + 1, i + 2});
}

void its_merge(indexed_triangle_set& A, const Vec3ds& triangles)
{
    std::vector<Vec3f> trianglesf;
    trianglesf.reserve(triangles.size());
    for (auto& t : triangles)
        trianglesf.emplace_back(t.cast<float>());

    its_merge(A, trianglesf);
}

} // namespace Slic3r::Domain
