#include "Slic3r/App/Render/GeometryBuilder.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec2f;

namespace Slic3r::App::Render {

using Domain::TriangleMesh;

const VertexAttribsDesc& VertexP3::format()
{
    static const VertexAttribsDesc desc = {
        {VertexAttribType::Vertex, DataType::Float, 3, 0}
    };
    return desc;
}

const VertexAttribsDesc& VertexP3N3::format()
{
    static const VertexAttribsDesc desc = {
        {VertexAttribType::Vertex, DataType::Float, 3, offsetof(VertexP3N3, position)},
        {VertexAttribType::Normal, DataType::Float, 3, offsetof(VertexP3N3, normal)}
    };
    return desc;
}

const VertexAttribsDesc& VertexP3T2::format()
{
    static const VertexAttribsDesc desc =  {
        {VertexAttribType::Vertex, DataType::Float, 3, offsetof(VertexP3T2, position)},
        {VertexAttribType::TexCoord0, DataType::Float, 2, offsetof(VertexP3T2, tex_coord)}
    };
    return desc;
}

const VertexAttribsDesc& VertexP3N3T2::format()
{
    static const VertexAttribsDesc desc =  {
        {VertexAttribType::Vertex, DataType::Float, 3, offsetof(VertexP3N3T2, position)},
        {VertexAttribType::Normal, DataType::Float, 3, offsetof(VertexP3N3T2, normal)},
        {VertexAttribType::TexCoord0, DataType::Float, 2, offsetof(VertexP3N3T2, tex_coord)}
    };
    return desc;
}

const VertexAttribsDesc& VertexP3N3I1::format()
{
    static const VertexAttribsDesc desc = {
        {VertexAttribType::Vertex, DataType::Float, 3, offsetof(VertexP3N3I1, position)},
        {VertexAttribType::Normal, DataType::Float, 3, offsetof(VertexP3N3I1, normal)},
        {VertexAttribType::PaletteIndex, DataType::Float, 1, offsetof(VertexP3N3I1, palette_index)}
    };
    return desc;
}

const VertexAttribsDesc& VertexP2T2::format()
{
    static const VertexAttribsDesc desc =  {
        {VertexAttribType::Vertex, DataType::Float, 2, offsetof(VertexP2T2, position)},
        {VertexAttribType::TexCoord0, DataType::Float, 2, offsetof(VertexP2T2, tex_coord)}
    };
    return desc;
}

std::unique_ptr<Geometry> geometry_from_triangle_mesh(
    Device& device, const indexed_triangle_set& triangle_mesh, const Material& material
)
{
    const size_t num_verts = triangle_mesh.indices.size() * 3;
    GeometryBuilder<VertexP3N3> builder;
    builder.reserve(num_verts, 0);

    for (const auto& tri : triangle_mesh.indices) {
        namespace tm = Biz::Algorithms::TriangleMesh;
        const auto n = tm::its_face_normal(triangle_mesh, tri);
        builder
            .add_vertex({triangle_mesh.vertices[tri[0]], n})
            .add_vertex({triangle_mesh.vertices[tri[1]], n})
            .add_vertex({triangle_mesh.vertices[tri[2]], n});
    }
    builder.add_draw_command({PrimitiveType::Triangles, 0, num_verts, material});
    return builder.build(device);
}

std::unique_ptr<Geometry> geometry_from_triangle_mesh(
    Device& device, const Domain::TriangleMesh& triangle_mesh, const Material& material
)
{
    return geometry_from_triangle_mesh(device, triangle_mesh.its, material);
}

std::unique_ptr<Geometry> geometry_from_triangles(
    Device& device, const std::vector<Vec3f>& triangles, const Material& material
)
{
    DEBUG_ASSERT(triangles.size() % 3 == 0);
    const size_t num_verts = triangles.size();
    GeometryBuilder<VertexP3N3> builder;
    builder.reserve(num_verts, 0);

    for (size_t i = 0; i < num_verts; i += 3) {
        const Vec3f& v0 = triangles[i + 0];
        const Vec3f& v1 = triangles[i + 1];
        const Vec3f& v2 = triangles[i + 2];
        Vec3f n = (v1 - v0).cross(v2 - v0).normalized();
        builder
            .add_vertex({ v0, n })
            .add_vertex({ v1, n })
            .add_vertex({ v2, n });
    }
    builder.add_draw_command({ PrimitiveType::Triangles, 0, num_verts, material });
    return builder.build(device);
}

std::unique_ptr<Geometry> geometry_from_triangles(
    Device& device, const std::vector<std::pair<Vec3f, Vec2f>>& triangles, const Material& material
)
{
    DEBUG_ASSERT(triangles.size() % 3 == 0);
    const size_t num_verts = triangles.size();
    GeometryBuilder<VertexP3N3T2> builder;
    builder.reserve(num_verts, 0);

    for (size_t i = 0; i < num_verts; i += 3) {
        const Vec3f& v0 = triangles[i + 0].first;
        const Vec3f& v1 = triangles[i + 1].first;
        const Vec3f& v2 = triangles[i + 2].first;
        Vec3f n = (v1 - v0).cross(v2 - v0).normalized();
        builder
            .add_vertex({ v0, n, triangles[i + 0].second })
            .add_vertex({ v1, n, triangles[i + 1].second })
            .add_vertex({ v2, n, triangles[i + 2].second });
    }
    builder.add_draw_command({ PrimitiveType::Triangles, 0, num_verts, material });
    return builder.build(device);
}

std::unique_ptr<Geometry> geometry_from_lines(
    Device& device, const std::vector<Vec3f>& lines, const Material& material
)
{
    DEBUG_ASSERT(lines.size() % 2 == 0);
    const size_t num_verts = lines.size();
    GeometryBuilder<VertexP3> builder;
    builder.reserve(num_verts, 0);

    for (size_t i = 0; i < num_verts; ++i) {
        builder.add_vertex({ lines[i] });
    }
    builder.add_draw_command({ PrimitiveType::Lines, 0, num_verts, material });
    return builder.build(device);
}

} // namespace Slic3r::App::Render
