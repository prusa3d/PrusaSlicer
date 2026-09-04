#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <numbers>

using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Index3;
namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace Slic3r::App::Scene {

namespace {

constexpr double TWO_PI = 2.0 * std::numbers::pi;
constexpr double HALF_PI = 0.5 * std::numbers::pi;

constexpr double AXIS_LINE_LENGTH = 30.0;
constexpr double CONE_RADIUS = 5.0;
constexpr double CONE_HEIGHT = 10.0;
constexpr double ANGLE_STEP = 2 * std::numbers::pi / 32; // 32 steps in full circle

constexpr uint8_t CIRCLE_RES = 64; // 64 steps in full circle
constexpr float CIRCLE_ANGLE_STEP = float(TWO_PI) / CIRCLE_RES;

constexpr uint8_t SPHERE_RES = 32; // 32 steps in full circle
constexpr float SPHERE_ANGLE_STEP = float(TWO_PI) / SPHERE_RES;

constexpr uint8_t CYLINDER_RES = 32; // 32 steps in full circle
constexpr float CYLINDER_ANGLE_STEP = float(TWO_PI) / CYLINDER_RES;

} // namespace

void GeometryDataFactory::create_data(GeometryDataId id)
{
    indexed_triangle_set its;

    switch (id) {

    case GeometryDataId::Segment:
        // creates a segment 1 unit long, along X axis
        create_segment();
        return;

    case GeometryDataId::Cone:
        // creates a cone contained into a 1x1x1 box 
        // the cone axis is the Z axis
        // the cone tip is in the direction of positive Z axis
        // the center of the cone base is at (0,0,0)
        its = TriMesh::its_make_cone(0.5, 1.0, ANGLE_STEP);
        break;

    case GeometryDataId::Cube:
        // creates an axis aligned 1x1x1 cube 
        its = TriMesh::its_make_cube(1.0, 1.0, 1.0);
        its_translate(its, -0.5f * Vec3f::Ones());
        break;

    case GeometryDataId::Circle:
        // creates a circle with diameter equal to 1 
        // the circle is contained into XY plane
        create_circle();
        return;

    case GeometryDataId::GradedCircle:
        // creates a graded circle with diameter equal to 1 
        // the circle is contained into XY plane
        create_graded_circle();
        return;

    case GeometryDataId::Sphere:
        // creates a sphere with diameter equal to 1 
        // the sphere center is (0,0,0)
        its = TriMesh::its_make_sphere(0.5, SPHERE_ANGLE_STEP);
        break;

    case GeometryDataId::SmoothSphere:
        // creates a sphere with smooth normals and diameter equal to 1
        // the sphere center is (0,0,0)
        create_smooth_sphere(0.5, SPHERE_ANGLE_STEP);
        return;

    case GeometryDataId::Cylinder:
//    case GizmoDataId::Cylinder: 
        // creates a cylinder contained into a 1x1x1 box 
        // the cylinder axis is the Z axis
        // the center of the cylinder base is at (0,0,0)
        its = TriMesh::its_make_cylinder(0.5, 1.0, SPHERE_ANGLE_STEP);
        break;

    case GeometryDataId::ToolMarker:
        // creates a tool marker (union of a cone + a cylinder)
        // with the tip of the cone in the origin (0,0,0) and the cylinder on top of the cone.
        // The object is aligned with the Z axis
        its = create_tool_marker();
        break;

    case GeometryDataId::CandyButton:
        // creates a candy button-like shape centered in the origin (0,0,0)
        create_candy_button();
        return;

    default:
        UNREACHABLE("Invalid GeometryDataId", id);
    }

    m_geometry_manager.set(id, Render::geometry_from_triangle_mesh(m_device, its, Render::Material()));
    m_triangle_mesh_manager.set(id, std::make_unique<TriangleMesh>(std::move(its)));
}

void GeometryDataFactory::create_segment()
{
    Render::GeometryBuilder<Render::VertexP3> builder;
    builder
        .add_vertex({{ 0.0f, 0.0f, 0.0f }})
        .add_vertex({{ 1.0f, 0.0f, 0.0f }})
        .add_draw_command({Render::PrimitiveType::Lines, 0, 2,  Render::Material{} });
    m_geometry_manager.set(GeometryDataId::Segment, builder.build(m_device));
}

void GeometryDataFactory::create_circle()
{
    Render::GeometryBuilder<Render::VertexP3> builder;
    for (uint8_t i = 0; i < CIRCLE_RES; ++i) {
        float angle = i * CIRCLE_ANGLE_STEP;
        builder.add_vertex({{ 0.5f * cosf(angle), 0.5f * sinf(angle), 0.0f }});
    }
    builder
        .add_draw_command({ Render::PrimitiveType::LineLoop, 0, CIRCLE_RES,  Render::Material{} });
    m_geometry_manager.set(GeometryDataId::Circle, builder.build(m_device));
}

void GeometryDataFactory::create_graded_circle()
{
    Render::GeometryBuilder<Render::VertexP3> builder;
    for (uint8_t i = 0; i < CIRCLE_RES; ++i) {
        float angle_i = i * CIRCLE_ANGLE_STEP;
        float angle_j = (i + 1) * CIRCLE_ANGLE_STEP;
        builder.add_vertex({ { 0.5f * cosf(angle_i), 0.5f * sinf(angle_i), 0.0f } });
        builder.add_vertex({ { 0.5f * cosf(angle_j), 0.5f * sinf(angle_j), 0.0f } });
    }
    float grade_step = float(TWO_PI) / CIRCLE_FINE_GRADE_SECONDARY_STEPS;
    for (uint8_t i = 0; i < CIRCLE_FINE_GRADE_SECONDARY_STEPS; ++i) {
        float angle_i = i * grade_step;
        float out_radius = (i % 2 == 0) ? 0.5f * CIRCLE_FINE_GRADE_PRIMARY_OUT_RADIUS : 0.5f * CIRCLE_FINE_GRADE_SECONDARY_OUT_RADIUS;
        builder.add_vertex({ { 0.5f * cosf(angle_i), 0.5f * sinf(angle_i), 0.0f } });
        builder.add_vertex({ { out_radius * cosf(angle_i), out_radius * sinf(angle_i), 0.0f } });
    }
    grade_step = float(TWO_PI) / CIRCLE_COARSE_GRADE_STEPS;
    for (uint8_t i = 0; i < CIRCLE_COARSE_GRADE_STEPS; ++i) {
        float angle_i = i * grade_step;
        builder.add_vertex({ { 0.5f * CIRCLE_COARSE_GRADE_IN_RADIUS * cosf(angle_i), 0.5f * CIRCLE_COARSE_GRADE_IN_RADIUS * sinf(angle_i), 0.0f } });
        builder.add_vertex({ { 0.5f * CIRCLE_COARSE_GRADE_OUT_RADIUS * cosf(angle_i), 0.5f * CIRCLE_COARSE_GRADE_OUT_RADIUS * sinf(angle_i), 0.0f } });
    }
    builder
        .add_draw_command({ Render::PrimitiveType::Lines, 0, builder.vertex_count(),  Render::Material{}});
    m_geometry_manager.set(GeometryDataId::GradedCircle, builder.build(m_device));
}

void GeometryDataFactory::create_smooth_sphere(double radius, double fa)
{
    Domain::TriangleMesh mesh = TriMesh::make_sphere(radius, fa);
    Render::GeometryBuilder<Render::VertexP3N3> builder;
    for (size_t i = 0; i < mesh.its.vertices.size(); ++i) {
        const Vec3f& v = mesh.its.vertices[i];
        builder.add_vertex({ v, v.normalized()});
    }
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        builder.add_index(mesh.its.indices[i][0]);
        builder.add_index(mesh.its.indices[i][1]);
        builder.add_index(mesh.its.indices[i][2]);
    }
    builder
        .add_draw_command({ Render::PrimitiveType::Triangles, 0, 3 * mesh.its.indices.size(),  Render::Material{} });
    m_geometry_manager.set(GeometryDataId::SmoothSphere, builder.build(m_device));
}

indexed_triangle_set GeometryDataFactory::create_tool_marker()
{
    static constexpr float RADIUS = 2.0f;
    static constexpr float CONE_HEIGHT = 4.0f;
    static constexpr float CYLINDER_HEIGHT = 8.0f;
    static constexpr uint8_t RESOLUTION = 32;
    static constexpr float STEP = float(TWO_PI) / RESOLUTION;

    indexed_triangle_set ret;
    ret.vertices.reserve(2 + 3 * RESOLUTION);
    ret.indices.reserve(4 * RESOLUTION);

    ret.vertices.emplace_back(Vec3f::Zero());
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        float angle_i = i * STEP;
        float cos_i = cos(angle_i);
        float sin_i = sin(angle_i);
        ret.vertices.emplace_back(Vec3f(RADIUS * cos_i, RADIUS * sin_i, CONE_HEIGHT));
    }
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        float angle_i = i * STEP;
        float cos_i = cos(angle_i);
        float sin_i = sin(angle_i);
        ret.vertices.emplace_back(Vec3f(RADIUS * cos_i, RADIUS * sin_i, CONE_HEIGHT + CYLINDER_HEIGHT));
    }
    ret.vertices.emplace_back(Vec3f(0.0f, 0.0f, CONE_HEIGHT + CYLINDER_HEIGHT));
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        float angle_i = i * STEP;
        float cos_i = cos(angle_i);
        float sin_i = sin(angle_i);
        ret.vertices.emplace_back(Vec3f(RADIUS * cos_i, RADIUS * sin_i, CONE_HEIGHT + CYLINDER_HEIGHT));
    }

    // cone triangles
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        ret.indices.emplace_back(Index3{ int(0), (i == RESOLUTION - 1) ? int(1) : int(i + 2), int(i + 1)});
    }
    // cylinder triangles
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        uint32_t v1 = i + 1;
        uint32_t v2 = (i == RESOLUTION - 1) ? 1 : i + 2;
        uint32_t v3 = (i == RESOLUTION - 1) ? 1 + RESOLUTION : i + RESOLUTION + 2;
        uint32_t v4 = i + RESOLUTION + 1;
        ret.indices.emplace_back(Index3{ int(v1), int(v2), int(v3) });
        ret.indices.emplace_back(Index3{ int(v1), int(v3), int(v4) });
    }
    // cylinder cap triangles
    uint32_t base = 1 + 2 * RESOLUTION;
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        ret.indices.emplace_back(Index3{ int(base), int(base + i + 1), (i == RESOLUTION - 1) ? int(base + 1) : int(base + i + 2) });
    }

    return ret;
}

void GeometryDataFactory::create_candy_button()
{
    Render::GeometryBuilder<Render::VertexP3N3> builder;

    static constexpr float RADIUS = 0.5f;
    static constexpr float HALF_HEIGHT = 0.5f;
    static constexpr uint8_t RESOLUTION = 32;
    static constexpr float ANGLE_STEP = float(TWO_PI) / RESOLUTION;

    builder.add_vertex({ { 0.0f, 0.0f, -HALF_HEIGHT }, -Vec3f::UnitZ() });
    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        float angle_i = i * ANGLE_STEP;
        float cos_i = cos(angle_i);
        float sin_i = sin(angle_i);
        builder.add_vertex({ { RADIUS * cos_i, RADIUS * sin_i, 0.0f }, { cos_i, sin_i, 0.0f } });
    }
    builder.add_vertex({ { 0.0f, 0.0f, HALF_HEIGHT }, Vec3f::UnitZ() });

    for (uint8_t i = 0; i < RESOLUTION; ++i) {
        builder.add_triangle_indices(0, (i == RESOLUTION - 1) ? 1 : i + 2, i + 1);
        builder.add_triangle_indices(i + 1, (i == RESOLUTION - 1) ? 1 : i + 2, RESOLUTION + 1);
    }

    builder
        .add_draw_command({ Render::PrimitiveType::Triangles, 0, builder.index_count(),  Render::Material{} });
    m_geometry_manager.set(GeometryDataId::CandyButton, builder.build(m_device));
}

} // namespace Slic3r::App::Scene
