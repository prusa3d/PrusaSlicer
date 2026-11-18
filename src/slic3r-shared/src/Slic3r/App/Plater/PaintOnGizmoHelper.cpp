#include "Slic3r/App/Plater/PaintOnGizmoHelper.hpp"

#include "Slic3r/App/Plater/MMPaintedVolumeRendering.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/TextureUnits.hpp"
#include "Slic3r/App/Scene/VertexPulledRenderNodeComponent.hpp"
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

#include <algorithm>
#include <array>
#include <numeric>
#include <span>

#include "libslic3r/TriangleMeshSlicer.hpp"

using Slic3r::App::Plater::PaintingPalette;
using Slic3r::App::Plater::PaletteEntry;
using Slic3r::App::Plater::TriangleSelectorRenderWrapper;
using Slic3r::App::Render::LinearDataTexture;
using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::PixelFormat;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polygon;
using Slic3r::Domain::Polygons;
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

/**
 * @brief Packs the triangle of a slot into its record, unpacked by fetch_triangle_record() in the
 * mm_gouraud_pulled shader.
 *
 * @return Indices of the vertices and state | seed fill << 8 | visible << 9, zero for a freed or
 * split triangle, which is not drawn.
 */
std::array<uint32_t, 4> pack_triangle_record(const TriangleSelector::Triangle& triangle)
{
    constexpr uint32_t TRIANGLE_RECORD_SEED_FILL_BIT = 1u << 8;
    constexpr uint32_t TRIANGLE_RECORD_VISIBLE_BIT   = 1u << 9;
    static_assert(TRIANGLE_STATE_TYPE_COUNT <= TRIANGLE_RECORD_SEED_FILL_BIT);

    // State and seed fill selection may be read only for a valid leaf.
    if (!triangle.valid() || triangle.is_split()) {
        return {0, 0, 0, 0};
    }

    uint32_t flags = static_cast<uint32_t>(triangle.get_state()) | TRIANGLE_RECORD_VISIBLE_BIT;
    if (triangle.is_selected_by_seed_fill()) {
        flags |= TRIANGLE_RECORD_SEED_FILL_BIT;
    }

    return {
        uint32_t(triangle.verts_idxs[0]),
        uint32_t(triangle.verts_idxs[1]),
        uint32_t(triangle.verts_idxs[2]),
        flags
    };
}

/**
 * @brief Half-open range [begin, end) of triangle or vertex slots.
 */
struct SlotRange
{
    size_t begin;
    size_t end;
};

/**
 * @brief Merged slot ranges of the changed chunks, which are in increasing order.
 */
std::vector<SlotRange>
slot_ranges_of_changed_chunks(const std::vector<int>& changed_chunks, const size_t slot_count)
{
    constexpr size_t chunk_size = TriangleSelector::SLOT_CHANGE_CHUNK_SIZE;

    std::vector<SlotRange> slot_ranges;
    for (const int chunk_idx : changed_chunks) {
        const size_t begin = static_cast<size_t>(chunk_idx) * chunk_size;
        if (begin >= slot_count) {
            break;
        }

        const size_t end = std::min(begin + chunk_size, slot_count);
        if (!slot_ranges.empty() && slot_ranges.back().end == begin) {
            slot_ranges.back().end = end;
        } else {
            slot_ranges.push_back({begin, end});
        }
    }

    return slot_ranges;
}

/**
 * @brief Color of every state followed by the colors of the triangles selected by seed fill.
 */
std::vector<ColorRGBA>
create_painting_palette_colors(const PaintingPalette& colors, const ColorRGBA& default_volume_color)
{
    std::vector<ColorRGBA> palette_colors(2 * TRIANGLE_STATE_TYPE_COUNT, default_volume_color);
    for (const PaletteEntry& entry : colors) {
        if (entry.state > 0 && entry.state < TRIANGLE_STATE_TYPE_COUNT) {
            palette_colors[entry.state] = entry.color;
        }
    }

    for (size_t state = 0; state < TRIANGLE_STATE_TYPE_COUNT; ++state) {
        palette_colors[TRIANGLE_STATE_TYPE_COUNT + state] =
            TriangleSelectorRenderWrapper::get_seed_fill_color(palette_colors[state]);
    }

    return palette_colors;
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

    const Render::Material material =
        Render::Material()
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
    m_triangle_records = std::make_unique<
        LinearDataTexture>(device, PixelFormat::RGBA32UI, "Painted triangle records");
    m_vertex_positions = std::make_unique<
        LinearDataTexture>(device, PixelFormat::RGBA32F, "Painted vertex positions");

    const Vec4f clipping_plane = {0.0f, 0.0f, 1.0f, FLT_MAX};
    const Vec2f z_range        = {Domain::SINKING_Z_THRESHOLD, FLT_MAX};

    m_painted_mesh_base_material =
        Render::Material{}
            .set_shader(device.context().shader_manager().shader("mm_gouraud_pulled"))
            .set_uniform("clipping_plane", clipping_plane)
            .set_uniform("z_range", z_range)
            .set_uniform("overhang.enabled", false)
            .set_uniform("overhang.max_normal_z", -1.f)
            .set_texture(
                Scene::TextureUnits::MULTI_MATERIAL_TRIANGLE_RECORDS,
                m_triangle_records->texture()
            )
            .set_uniform(
                "triangle_records_tex",
                Scene::TextureUnits::MULTI_MATERIAL_TRIANGLE_RECORDS
            )
            .set_texture(
                Scene::TextureUnits::MULTI_MATERIAL_VERTEX_POSITIONS,
                m_vertex_positions->texture()
            )
            .set_uniform(
                "vertex_positions_tex",
                Scene::TextureUnits::MULTI_MATERIAL_VERTEX_POSITIONS
            );

    Scene::NodeBuilder painted_mesh_node_builder{scene};
    painted_mesh_node_builder.set_debug_name("TriangleSelector - Painted mesh node")
        .set_transform(world_trafo)
        .set_vertex_pulling(
            device,
            Render::DrawCommand(Render::PrimitiveType::Triangles, 0, 0, Render::Material{}),
            m_painted_mesh_base_material,
            int(PlaterSceneLayer::DocumentObjects)
        )
        .set_aabb(m_aabb_mesh);

    std::unique_ptr<Scene::Node> painted_mesh_node = painted_mesh_node_builder.build();
    m_painted_mesh_render_component = dynamic_cast<Scene::VertexPulledRenderNodeComponent*>(
        painted_mesh_node->render_component()
    );
    ASSERT(m_painted_mesh_render_component != nullptr);
    scene.add_child(painted_mesh_node.release(), &parent_node);

    this->update_painting_palette(device);
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

void TriangleSelectorRenderWrapper::
    upload_changed_painting(Render::Device& device, const bool seed_fill_contour_shown)
{
    const std::vector<int> changed_triangle_chunks =
        m_triangle_selector.take_changed_triangle_slot_chunks();
    const std::vector<int> changed_vertex_chunks =
        m_triangle_selector.take_changed_vertex_slot_chunks();

    const TriangleSelector::Triangles& triangles = m_triangle_selector.triangles();
    const TriangleSelector::Vertices& vertices   = m_triangle_selector.vertices();

    // A reallocated texture lost its content, so all of it is uploaded.
    const bool triangle_records_reallocated = m_triangle_records->reserve_texels(triangles.size());
    const std::vector<SlotRange> triangle_ranges = triangle_records_reallocated ?
        std::vector<SlotRange>{{0, triangles.size()}} :
        slot_ranges_of_changed_chunks(changed_triangle_chunks, triangles.size());

    for (const SlotRange& triangle_range : triangle_ranges) {
        const std::span<const TriangleSelector::Triangle> range_triangles(
            triangles.data() + triangle_range.begin,
            triangle_range.end - triangle_range.begin
        );

        m_packed_triangle_records.resize(range_triangles.size());
        std::ranges::
            transform(range_triangles, m_packed_triangle_records.begin(), pack_triangle_record);

        m_triangle_records->upload_texels(
            triangle_range.begin,
            m_packed_triangle_records.size(),
            m_packed_triangle_records.data()
        );
    }

    // The vertices are uploaded straight from the TriangleSelector without repacking, so the fourth
    // component holds their reference count, which the shader does not read.
    static_assert(sizeof(TriangleSelector::Vertex) == 4 * sizeof(float));
    const bool vertex_positions_reallocated = m_vertex_positions->reserve_texels(vertices.size());
    const std::vector<SlotRange> vertex_ranges = vertex_positions_reallocated ?
        std::vector<SlotRange>{{0, vertices.size()}} :
        slot_ranges_of_changed_chunks(changed_vertex_chunks, vertices.size());

    for (const SlotRange& vertex_range : vertex_ranges) {
        m_vertex_positions->upload_texels(
            vertex_range.begin,
            vertex_range.end - vertex_range.begin,
            &vertices[vertex_range.begin]
        );
    }

    m_painted_mesh_render_component->set_total_vertex_count(3 * triangles.size());

    if (!changed_triangle_chunks.empty()) {
        m_painted_contour_outdated = true;
    }

    m_painted_contour_node->set_enabled(seed_fill_contour_shown);
    if (seed_fill_contour_shown && m_painted_contour_outdated) {
        this->update_painted_contour_geometry(device);
        m_painted_contour_outdated = false;
    }
}

void TriangleSelectorRenderWrapper::update_painting_palette(Render::Device& device)
{
    MMPainting::apply_mm_palette_to_material(
        device,
        create_painting_palette_colors(m_colors, m_default_volume_color),
        m_painted_mesh_base_material
    );
    m_painted_mesh_render_component->replace_material(m_painted_mesh_base_material);
}

void TriangleSelectorRenderWrapper::set_clipping_plane(const Vec4f& clipping_plane)
{
    m_painted_mesh_base_material.set_uniform("clipping_plane", clipping_plane);
    m_painted_mesh_render_component->replace_material(m_painted_mesh_base_material);
}

void TriangleSelectorRenderWrapper::set_overhang_slope_normal(const float angle_in_degrees)
{
    const float max_normal_z = -std::cos(Slic3r::deg2rad(angle_in_degrees));

    m_painted_mesh_base_material.set_uniform("overhang.enabled", angle_in_degrees > 0.f);
    m_painted_mesh_base_material.set_uniform("overhang.max_normal_z", max_normal_z);
    m_painted_mesh_render_component->replace_material(m_painted_mesh_base_material);
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
    m_material =
        Render::Material{}
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

void CircleCursorRenderWrapper::
    init_cursor_node(Render::Device& device, Scene::Scene& scene, Scene::Node& parent_node)
{
    Render::Material material =
        Render::Material{}
            .set_shader(device.context().shader_manager().shader("flat"))
            .set_uniform("uniform_color", ColorRGBA{0.f, 1.f, 0.3f, 1.f});

    m_cursor_geometry = std::make_unique<Render::Geometry>(device);
    m_cached_camera_zoom.reset();

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

void HeightRangeCursorRenderWrapper::
    init_cursor_node(Render::Device& device, Scene::Scene& scene, Scene::Node& parent_node)
{
    Render::Material material =
        Render::Material{}
            .set_shader(device.context().shader_manager().shader("mm_contour"))
            .set_uniform("uniform_color", ColorRGBA::WHITE())
            .set_uniform("offset", 0.00001f);

    m_cursor_geometry = std::make_unique<Render::Geometry>(device);
    m_cached_cursor_position.reset();
    m_cached_z_range.reset();

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
