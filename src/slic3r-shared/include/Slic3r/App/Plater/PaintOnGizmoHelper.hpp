#pragma once

#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/LinearDataTexture.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <array>
#include <optional>
#include <vector>

namespace Slic3r::App::Scene {
class VertexPulledRenderNodeComponent;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {

class TriangleSelectorRenderWrapper
{
public:
    TriangleSelectorRenderWrapper() = delete;
    TriangleSelectorRenderWrapper(
        const Domain::TriangleMesh& triangle_mesh,
        const AABBMesh& aabb_mesh,
        const PaintingPalette& colors,
        const Domain::ColorRGBA& default_volume_color
    );
    virtual ~TriangleSelectorRenderWrapper() = default;

    TriangleSelectorRenderWrapper(TriangleSelectorRenderWrapper&&) = default;

    static Domain::ColorRGBA get_seed_fill_color(const Domain::ColorRGBA& base_color);

    void init_painted_mesh_node(
        Render::Device& device,
        Scene::Scene& scene,
        Scene::Node& parent_node,
        const Domain::Transform3d& world_trafo
    );
    void init_painted_contour_node(
        Render::Device& device,
        Scene::Scene& scene,
        Scene::Node& parent_node,
        const Domain::Transform3d& world_trafo
    );

    /**
     * @brief Uploads the triangles and vertices of the triangle selector changed since the last
     * upload and rebuilds the changed seed fill contour, only when it is shown.
     */
    void upload_changed_painting(Render::Device& device, bool seed_fill_contour_shown);

    /**
     * @brief Uploads the palette after the painting colors changed.
     */
    void update_painting_palette(Render::Device& device);

    void set_clipping_plane(const Domain::Vec4f& clipping_plane);
    void set_overhang_slope_normal(float angle_in_degrees);

    const Biz::Algorithms::TriangleSelector& triangle_selector() const;
    Biz::Algorithms::TriangleSelector& triangle_selector();

private:
    void update_painted_contour_geometry(Render::Device& device);

    Biz::Algorithms::TriangleSelector m_triangle_selector;
    const AABBMesh& m_aabb_mesh;
    const PaintingPalette& m_colors;
    const Domain::ColorRGBA m_default_volume_color;

    Render::Material m_painted_mesh_base_material;

    std::unique_ptr<Render::LinearDataTexture> m_triangle_records;
    std::unique_ptr<Render::LinearDataTexture> m_vertex_positions;
    std::unique_ptr<Render::Geometry> m_painted_contour_geometry;

    // Kept to reuse its memory between the uploads.
    std::vector<std::array<uint32_t, 4>> m_packed_triangle_records;
    // The triangles changed since the contour was built.
    bool m_painted_contour_outdated = false;

    Scene::VertexPulledRenderNodeComponent* m_painted_mesh_render_component = nullptr;
    Scene::Node* m_painted_contour_node                                     = nullptr;
};

class CursorRenderWrapper
{
public:
    CursorRenderWrapper()          = default;
    virtual ~CursorRenderWrapper() = default;

    virtual void set_enabled(bool enabled);

protected:
    Scene::Node* m_cursor_node = nullptr;
};

class SphereCursorRenderWrapper : public CursorRenderWrapper
{
public:
    void init_cursor_node(
        Render::Device& device,
        Scene::Scene& scene,
        Scene::Node& parent_node,
        Scene::GeometryDataFactory& data_factory
    );

    void update_cursor_geometry(
        const Domain::Vec3d& cursor_position,
        double cursor_radius,
        const Domain::ColorRGBA& cursor_color
    );

private:
    Render::Material m_material;
    const Render::Geometry* m_cursor_geometry = nullptr;
};

class CircleCursorRenderWrapper : public CursorRenderWrapper
{
public:
    void init_cursor_node(Render::Device& device, Scene::Scene& scene, Scene::Node& parent_node);

    void update_cursor_geometry(
        Render::Device& device,
        const Scene::Camera& camera,
        const Domain::Vec3d& cursor_position,
        double cursor_radius
    );

private:
    std::unique_ptr<Render::Geometry> m_cursor_geometry;

    std::optional<double> m_cached_camera_zoom;
};

class HeightRangeCursorRenderWrapper : public CursorRenderWrapper
{
public:
    void init_cursor_node(Render::Device& device, Scene::Scene& scene, Scene::Node& parent_node);

    void update_cursor_geometry(
        Render::Device& device,
        const Domain::Vec3d& cursor_position,
        double z_range,
        const Domain::TriangleMesh& triangle_mesh,
        const Domain::Transform3d& trafo
    );

private:
    std::unique_ptr<Render::Geometry> m_cursor_geometry;

    std::optional<Domain::Vec3d> m_cached_cursor_position;
    std::optional<double> m_cached_z_range;
};

} // namespace Slic3r::App::Plater
