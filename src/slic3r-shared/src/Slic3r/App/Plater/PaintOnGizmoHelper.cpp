#include "Slic3r/App/Plater/PaintOnGizmoHelper.hpp"

#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Utils.hpp"

#include <array>
#include <numeric>
#include <optional>

#include "libslic3r/TriangleMeshSlicer.hpp"

using Slic3r::App::Plater::PaintingPalette;
using Slic3r::App::Plater::PaletteEntry;
using Slic3r::App::Plater::TriangleSelectorRenderWrapper;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polygon;
using Slic3r::Domain::Polygons;
using Slic3r::Domain::SquareMatrix3f;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec4f;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace {

std::unique_ptr<App::Render::Geometry> create_painted_mesh_geometry(
    App::Render::Device& device,
    const Biz::Algorithms::TriangleSelector& triangle_selector,
    const PaintingPalette& colors,
    const ColorRGBA& default_volume_color
)
{
    using namespace Slic3r::App;
    using namespace Slic3r::Biz::Algorithms;

    const TriangleSelector::Vertices& vertices   = triangle_selector.vertices();
    const TriangleSelector::Triangles& triangles = triangle_selector.triangles();

    Render::GeometryBuilder<Render::VertexP3> geometry_builder;
    geometry_builder.reserve(vertices.size(), 3 * triangles.size());

    for (const TriangleSelector::Vertex& vertex : vertices) {
        geometry_builder.add_vertex(Render::VertexP3(vertex.v));
    }

    std::array<std::optional<ColorRGBA>, TRIANGLE_STATE_TYPE_COUNT> color_per_state;
    for (const PaletteEntry& entry : colors) {
        if (entry.state > 0 && entry.state < color_per_state.size()) {
            color_per_state[entry.state] = entry.color;
        }
    }

    // The first half of the buckets holds the plain triangles indexed by their state and the
    // second half the ones selected by the seed fill.
    std::vector<std::vector<uint32_t>> indices_per_colors(2 * TRIANGLE_STATE_TYPE_COUNT);
    for (const TriangleSelector::Triangle& tr : triangles) {
        if (tr.valid() && !tr.is_split()) {
            const size_t triangle_state = static_cast<size_t>(tr.get_state());
            //assert(triangle_state < state_colors.size());

            const size_t color = color_per_state[triangle_state].has_value() ? triangle_state : 0;
            std::vector<uint32_t>& indices_per_color = indices_per_colors
                [color + tr.is_selected_by_seed_fill() * TRIANGLE_STATE_TYPE_COUNT];

            if (indices_per_color.size() + 3 > indices_per_color.capacity()) {
                indices_per_color.reserve(
                    Slic3r::next_highest_power_of_2(indices_per_color.size() + 3)
                );
            }

            indices_per_color.emplace_back(tr.verts_idxs[0]);
            indices_per_color.emplace_back(tr.verts_idxs[1]);
            indices_per_color.emplace_back(tr.verts_idxs[2]);
        }
    }

    const auto get_color_from_color_idx = [&](const size_t color_idx) -> ColorRGBA
    {
        const bool is_seed_fill = color_idx >= TRIANGLE_STATE_TYPE_COUNT;
        const size_t state      = is_seed_fill ? color_idx - TRIANGLE_STATE_TYPE_COUNT : color_idx;
        const ColorRGBA color   = color_per_state[state].value_or(default_volume_color);

        return is_seed_fill ? TriangleSelectorRenderWrapper::get_seed_fill_color(color) : color;
    };

    size_t indices_offset = 0;
    for (std::vector<uint32_t>& indices_per_color : indices_per_colors) {
        if (indices_per_color.empty()) {
            continue;
        }

        const size_t color_idx   = &indices_per_color - indices_per_colors.data();
        const size_t num_indices = indices_per_color.size();

        geometry_builder.add_indices(std::move(indices_per_color));

        const Render::Material material =
            Render::Material().set_uniform("uniform_color", get_color_from_color_idx(color_idx));
        geometry_builder.add_draw_command(
            Render::DrawCommand(
                Render::PrimitiveType::Triangles,
                indices_offset,
                num_indices,
                material
            )
        );

        indices_offset += num_indices;
    }

    return geometry_builder.build(device);
}

std::unique_ptr<App::Render::Geometry> create_painted_contour_geometry(
    App::Render::Device& device,
    const Biz::Algorithms::TriangleSelector& triangle_selector
)
{
    using namespace Slic3r::App;
    using namespace Slic3r::Biz::Algorithms;

    const TriangleSelector::Vertices& vertices = triangle_selector.vertices();

    const std::vector<Domain::Index2> contour_edges = triangle_selector.get_seed_fill_contour();

    Render::GeometryBuilder<Render::VertexP3> geometry_builder;
    geometry_builder.reserve(2 * contour_edges.size(), 0);

    for (const Domain::Index2& edge : contour_edges) {
        geometry_builder.add_vertex(Render::VertexP3(vertices[edge[0]].v));
        geometry_builder.add_vertex(Render::VertexP3(vertices[edge[1]].v));
    }

    const Render::Material material = Render::Material()
                                          .set_uniform("uniform_color", ColorRGBA::WHITE())
                                          .set_uniform("offset", 0.00001f);
    geometry_builder.add_draw_command(
        Render::DrawCommand(Render::PrimitiveType::Lines, 0, 2 * contour_edges.size(), material)
    );

    return geometry_builder.build(device);
}

std::unique_ptr<App::Render::Geometry>
create_dashed_circle_cursor_geometry(App::Render::Device& device, const App::Scene::Camera& camera)
{
    using namespace Slic3r::App;

    const double current_zoom = camera.zoom();
    const unsigned int steps_count =
        (unsigned int) (2 * (4 + int(252 * (current_zoom - 1.0f) / (100.0f - 1.0f))));
    const float step_size = 2.f * std::numbers::pi_v<float> / float(steps_count);

    Render::GeometryBuilder<Render::VertexP3> geometry_builder;
    geometry_builder.reserve(steps_count, steps_count);

    for (unsigned int i = 0; i < steps_count; ++i) {
        if (i % 2 != 0) {
            continue;
        }

        const float angle_i  = float(i) * step_size;
        const unsigned int j = (i + 1) % steps_count;
        const float angle_j  = float(j) * step_size;

        geometry_builder.add_vertex(
            Render::VertexP3(Vec3f(0.5f * std::cos(angle_i), 0.5f * std::sin(angle_i), 0.f))
        );
        geometry_builder.add_vertex(
            Render::VertexP3(Vec3f(0.5f * std::cos(angle_j), 0.5f * std::sin(angle_j), 0.f))
        );
    }

    geometry_builder.add_draw_command(
        Render::DrawCommand(Render::PrimitiveType::Lines, 0, steps_count, Render::Material{})
    );

    return geometry_builder.build(device);
}

std::unique_ptr<App::Render::Geometry> create_height_range_cursor_geometry(
    App::Render::Device& device,
    const Vec3d& cursor_position,
    const double height_range_z_range,
    const Domain::TriangleMesh& triangle_mesh,
    const Transform3d& trafo
)
{
    using namespace Slic3r::App;

    const Vec3d mesh_hit_world          = trafo * cursor_position;
    const std::array<double, 2> z_range = {
        mesh_hit_world.z() - height_range_z_range / 2.f,
        mesh_hit_world.z() + height_range_z_range / 2.f
    };

    struct SlicedPolygonsAtZ
    {
        float z;
        Polygons polygons;
    };

    std::vector<SlicedPolygonsAtZ> sliced_polygons_per_z;
    for (const double z : z_range) {
        sliced_polygons_per_z.push_back(
            {static_cast<float>(z),
             slice_mesh(triangle_mesh.its, static_cast<float>(z), MeshSlicingParams(trafo))}
        );
    }

    const size_t max_vertices_cnt = std::accumulate(
        sliced_polygons_per_z.cbegin(),
        sliced_polygons_per_z.cend(),
        0,
        [](const size_t sum, const SlicedPolygonsAtZ& polygons_at_z)
        { return sum + Algorithms::Polygon::count_points(polygons_at_z.polygons); }
    );

    Render::GeometryBuilder<Render::VertexP3> geometry_builder;
    geometry_builder.reserve(2 * max_vertices_cnt, 0);

    size_t vertices_cnt = 0;
    for (const SlicedPolygonsAtZ& polygons_at_z : sliced_polygons_per_z) {
        for (const Polygon& polygon : polygons_at_z.polygons) {
            if (polygon.size() <= 1) {
                continue;
            }

            for (size_t pt_idx = 0; pt_idx < polygon.size(); ++pt_idx) {
                const Point& pt1 = polygon.points[pt_idx];
                const Point& pt2 = polygon.points[(pt_idx + 1) % polygon.size()];

                geometry_builder.add_vertex(
                    Render::VertexP3(Vec3f(
                        Algorithms::Scaling::unscaled<float>(pt1.x()),
                        Algorithms::Scaling::unscaled<float>(pt1.y()),
                        polygons_at_z.z
                    ))
                );
                geometry_builder.add_vertex(
                    Render::VertexP3(Vec3f(
                        Algorithms::Scaling::unscaled<float>(pt2.x()),
                        Algorithms::Scaling::unscaled<float>(pt2.y()),
                        polygons_at_z.z
                    ))
                );
            }

            vertices_cnt += 2 * polygon.size();
        }
    }

    geometry_builder.add_draw_command(
        Render::DrawCommand(Render::PrimitiveType::Lines, 0, vertices_cnt, Render::Material{})
    );

    return geometry_builder.build(device);
}

} // namespace

namespace Slic3r::App::Plater {
TriangleSelectorRenderWrapper::TriangleSelectorRenderWrapper(
    const Domain::TriangleMesh& triangle_mesh,
    const AABBMesh& aabb_mesh,
    const PaintingPalette& colors,
    const ColorRGBA& default_volume_color
) :
    m_triangle_selector(triangle_mesh),
    m_aabb_mesh(aabb_mesh),
    m_colors(colors),
    m_default_volume_color(default_volume_color)
{}

const Biz::Algorithms::TriangleSelector& TriangleSelectorRenderWrapper::triangle_selector() const
{
    return m_triangle_selector;
}

Biz::Algorithms::TriangleSelector& TriangleSelectorRenderWrapper::triangle_selector()
{
    return m_triangle_selector;
}

ColorRGBA TriangleSelectorRenderWrapper::get_seed_fill_color(const ColorRGBA& base_color)
{
    return Biz::Algorithms::Color::saturate(base_color, 0.75f);
}

void TriangleSelectorRenderWrapper::init_painted_mesh_node(
    Render::Device& device,
    Scene::Scene& scene,
    Scene::Node& parent_node,
    const Transform3d& world_trafo
)
{
    const Vec4f clipping_plane = {0.0f, 0.0f, 1.0f, FLT_MAX};
    const Vec2f z_range        = {Domain::SINKING_Z_THRESHOLD, FLT_MAX};
    const SquareMatrix3f world_normal_matrix =
        world_trafo.linear().inverse().transpose().cast<float>();

    m_painted_mesh_base_material =
        Render::Material{}
            .set_shader(device.context().shader_manager().shader("mm_gouraud"))
            .set_uniform("clipping_plane", clipping_plane)
            .set_uniform("z_range", z_range)
            .set_uniform("overhang.enabled", false)
            .set_uniform("overhang.world_normal_matrix", world_normal_matrix)
            .set_uniform("overhang.max_normal_z", -1.f);

    m_painted_mesh_geometry =
        create_painted_mesh_geometry(device, m_triangle_selector, m_colors, m_default_volume_color);

    Scene::NodeBuilder painted_mesh_node_builder{scene};
    painted_mesh_node_builder.set_debug_name("TriangleSelector - Painted mesh node")
        .set_transform(world_trafo)
        .set_mesh(
            m_painted_mesh_geometry.get(),
            m_painted_mesh_base_material,
            int(PlaterSceneLayer::DocumentObjects)
        )
        .set_aabb(m_aabb_mesh);

    std::unique_ptr<Scene::Node> painted_mesh_node = painted_mesh_node_builder.build();
    m_painted_mesh_node                            = painted_mesh_node.get();
    scene.add_child(painted_mesh_node.release(), &parent_node);
}

void TriangleSelectorRenderWrapper::init_painted_contour_node(
    Render::Device& device,
    Scene::Scene& scene,
    Scene::Node& parent_node,
    const Transform3d& world_trafo
)
{
    const Render::Material material =
        Render::Material{}.set_shader(device.context().shader_manager().shader("mm_contour"));
    m_painted_contour_geometry = create_painted_contour_geometry(device, m_triangle_selector);

    Scene::NodeBuilder painted_contour_node_builder{scene};
    painted_contour_node_builder.set_debug_name("TriangleSelector - Painted contour node")
        .set_transform(world_trafo)
        .set_mesh(
            m_painted_contour_geometry.get(),
            material,
            int(PlaterSceneLayer::DocumentObjects)
        );

    std::unique_ptr<Scene::Node> painted_contour_node = painted_contour_node_builder.build();
    m_painted_contour_node                            = painted_contour_node.get();
    scene.add_child(painted_contour_node.release(), &parent_node);
}

void TriangleSelectorRenderWrapper::update_painted_mesh_geometry(
    Render::Device& device,
    const std::optional<Render::Material>& new_material
)
{
    ASSERT(
        m_painted_mesh_node != nullptr
        && m_painted_mesh_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component())
            != nullptr
    );

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component());
    std::unique_ptr<Render::Geometry> new_geometry = create_painted_mesh_geometry(
        device,
        this->m_triangle_selector,
        m_colors,
        m_default_volume_color
    );
    render_component->set_geometry(new_geometry.get());
    m_painted_mesh_geometry = std::move(new_geometry);

    if (new_material.has_value()) {
        render_component->replace_material(new_material.value());
    }
}

void TriangleSelectorRenderWrapper::set_clipping_plane(const Vec4f& clipping_plane)
{
    ASSERT(
        m_painted_mesh_node != nullptr
        && m_painted_mesh_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component())
            != nullptr
    );

    m_painted_mesh_base_material.set_uniform("clipping_plane", clipping_plane);

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component());
    render_component->replace_material(m_painted_mesh_base_material);
}

void TriangleSelectorRenderWrapper::set_overhang_slope_normal(const float angle_in_degrees)
{
    ASSERT(
        m_painted_mesh_node != nullptr
        && m_painted_mesh_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component())
            != nullptr
    );

    const float max_normal_z = -std::cos(Slic3r::deg2rad(angle_in_degrees));

    m_painted_mesh_base_material.set_uniform("overhang.enabled", angle_in_degrees > 0.f);
    m_painted_mesh_base_material.set_uniform("overhang.max_normal_z", max_normal_z);

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_mesh_node->render_component());
    render_component->replace_material(m_painted_mesh_base_material);
}

void TriangleSelectorRenderWrapper::update_painted_contour_geometry(Render::Device& device)
{
    ASSERT(
        m_painted_contour_node != nullptr
        && m_painted_contour_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_contour_node->render_component())
            != nullptr
    );

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_painted_contour_node->render_component());
    std::unique_ptr<Render::Geometry> new_geometry =
        create_painted_contour_geometry(device, this->m_triangle_selector);
    render_component->set_geometry(new_geometry.get());
    m_painted_contour_geometry = std::move(new_geometry);
}

void TriangleSelectorRenderWrapper::update_painted_geometry(Render::Device& device)
{
    this->update_painted_mesh_geometry(device);
    this->update_painted_contour_geometry(device);
}

void CursorRenderWrapper::set_enabled(const bool enabled)
{
    ASSERT(m_cursor_node != nullptr);
    m_cursor_node->set_enabled(enabled);
}

void SphereCursorRenderWrapper::init_cursor_node(
    Render::Device& device,
    Scene::Scene& scene,
    Scene::Node& parent_node,
    Scene::GeometryDataFactory& data_factory
)
{
    m_material = Render::Material{}
                     .set_shader(device.context().shader_manager().shader("flat"))
                     .set_transparent(true);
    m_cursor_geometry = data_factory.geometry(Scene::GeometryDataId::Sphere);

    Scene::NodeBuilder sphere_cursor_node_builder{scene};
    sphere_cursor_node_builder.set_debug_name("PaintOnGizmoBase - Sphere cursor node")
        .set_mesh(m_cursor_geometry, m_material, int(PlaterSceneLayer::DocumentObjects))
        .set_enabled(false);

    std::unique_ptr<Scene::Node> sphere_cursor_node = sphere_cursor_node_builder.build();
    m_cursor_node                                   = sphere_cursor_node.get();
    scene.add_child(sphere_cursor_node.release(), &parent_node);
}

void SphereCursorRenderWrapper::update_cursor_geometry(
    const Vec3d& cursor_position,
    const double cursor_radius,
    const Domain::ColorRGBA& cursor_color
)
{
    ASSERT(
        m_cursor_node != nullptr
        && m_cursor_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component())
            != nullptr
    );

    Domain::Transform3d trafo = Domain::Transform3d::Identity();
    trafo.translate(cursor_position);
    trafo.scale(2. * cursor_radius);

    m_material.set_uniform("uniform_color", cursor_color);

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component());
    render_component->replace_material(m_material);
    m_cursor_node->set_local_transform(trafo);
}

void CircleCursorRenderWrapper::init_cursor_node(
    Render::Device& device,
    Scene::Scene& scene,
    Scene::Node& parent_node
)
{
    Render::Material material = Render::Material{}
                                    .set_shader(device.context().shader_manager().shader("flat"))
                                    .set_uniform("uniform_color", ColorRGBA{0.f, 1.f, 0.3f, 1.f});

    m_cursor_geometry = std::make_unique<Render::Geometry>(device);

    Scene::NodeBuilder circle_cursor_node_builder{scene};
    circle_cursor_node_builder.set_debug_name("PaintOnGizmoBase - Circle cursor node")
        .set_mesh(m_cursor_geometry.get(), material, int(PlaterSceneLayer::AlwaysOnTop))
        .set_enabled(false);

    std::unique_ptr<Scene::Node> circle_cursor_node = circle_cursor_node_builder.build();
    m_cursor_node                                   = circle_cursor_node.get();
    scene.add_child(circle_cursor_node.release(), &parent_node);
}

void CircleCursorRenderWrapper::update_cursor_geometry(
    Render::Device& device,
    const Scene::Camera& camera,
    const Vec3d& cursor_position,
    const double cursor_radius
)
{
    ASSERT(
        m_cursor_node != nullptr
        && m_cursor_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component())
            != nullptr
    );

    if (!m_cached_camera_zoom.has_value() || m_cached_camera_zoom.value() != camera.zoom()) {
        m_cached_camera_zoom = camera.zoom();

        Scene::MeshRenderNodeComponent* render_component =
            dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component());
        std::unique_ptr<Render::Geometry> new_geometry =
            create_dashed_circle_cursor_geometry(device, camera);
        render_component->set_geometry(new_geometry.get());
        m_cursor_geometry = std::move(new_geometry);
    }

    Domain::Transform3d trafo = Domain::Transform3d::Identity();
    trafo.translate(cursor_position);
    trafo.rotate(Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitZ(), -camera.forward()));
    trafo.scale(2. * cursor_radius);

    m_cursor_node->set_local_transform(trafo);
}

void HeightRangeCursorRenderWrapper::init_cursor_node(
    Render::Device& device,
    Scene::Scene& scene,
    Scene::Node& parent_node
)
{
    Render::Material material =
        Render::Material{}
            .set_shader(device.context().shader_manager().shader("mm_contour"))
            .set_uniform("uniform_color", ColorRGBA::WHITE())
            .set_uniform("offset", 0.00001f);

    m_cursor_geometry = std::make_unique<Render::Geometry>(device);

    Scene::NodeBuilder height_range_cursor_node_builder{scene};
    height_range_cursor_node_builder.set_debug_name("PaintOnGizmoBase - HeightRange cursor node")
        .set_mesh(m_cursor_geometry.get(), material, int(PlaterSceneLayer::DocumentObjects))
        .set_enabled(false);

    std::unique_ptr<Scene::Node> height_range_cursor_node =
        height_range_cursor_node_builder.build();
    m_cursor_node = height_range_cursor_node.get();
    scene.add_child(height_range_cursor_node.release(), &parent_node);
}

void HeightRangeCursorRenderWrapper::update_cursor_geometry(
    Render::Device& device,
    const Vec3d& cursor_position,
    const double z_range,
    const Domain::TriangleMesh& triangle_mesh,
    const Transform3d& trafo
)
{
    ASSERT(
        m_cursor_node != nullptr
        && m_cursor_node->has_render_component()
        && dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component())
            != nullptr
    );

    if (m_cached_cursor_position.has_value()
        && m_cached_cursor_position.value() == cursor_position
        && m_cached_z_range.has_value()
        && m_cached_z_range.value() == z_range)
    {
        return;
    }

    m_cached_cursor_position = cursor_position;
    m_cached_z_range         = z_range;

    Scene::MeshRenderNodeComponent* render_component =
        dynamic_cast<Scene::MeshRenderNodeComponent*>(m_cursor_node->render_component());
    std::unique_ptr<Render::Geometry> new_geometry =
        create_height_range_cursor_geometry(device, cursor_position, z_range, triangle_mesh, trafo);
    render_component->set_geometry(new_geometry.get());
    m_cursor_geometry = std::move(new_geometry);
}

} // namespace Slic3r::App::Plater
