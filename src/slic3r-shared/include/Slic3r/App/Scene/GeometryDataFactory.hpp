#pragma once

#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/Scene.hpp"

namespace Slic3r::App::Scene {

constexpr uint8_t CIRCLE_COARSE_GRADE_STEPS = 8; // 8 steps = 45 degrees step angle
constexpr uint8_t CIRCLE_FINE_GRADE_SECONDARY_STEPS = 72; // 72 steps = 5 degrees step angle
constexpr float CIRCLE_FINE_GRADE_PRIMARY_OUT_RADIUS = 1.1f;
constexpr float CIRCLE_FINE_GRADE_SECONDARY_OUT_RADIUS = 1.05f;
constexpr float CIRCLE_COARSE_GRADE_IN_RADIUS = 1.0f / 3.0f;
constexpr float CIRCLE_COARSE_GRADE_OUT_RADIUS = 2.0f / 3.0f;

enum class GeometryDataId
{
    Segment = 0,
    Cone = 1,
    Cube = 2,
    Circle = 3,
    GradedCircle = 4,
    Sphere = 5,
    SmoothSphere = 6,
    Cylinder = 7,
    ToolMarker = 8,
    CandyButton = 9,
};

class GeometryDataFactory
{
public:
    using MeshManager = TriangleMeshManager<GeometryDataId>;
    using GeometryManager = Render::GeometryManager<GeometryDataId>;

    explicit GeometryDataFactory(Render::Device& device, std::string debug_name) :
        m_device{device},
        m_triangle_mesh_manager{debug_name + "_mesh"},
        m_geometry_manager{debug_name + "_geometry"}
    {}

    /**
     * @brief Get or create a triangle mesh.
     *
     *
     * @param id Geometry data identifier to get triangle mesh for
     * @return A pointer to Scene::TriangleMesh instance or `nullptr` if there is no such for given
     * gizmo data.
     */
    const TriangleMesh* triangle_mesh(GeometryDataId id)
    {
        auto triangle_mesh = m_triangle_mesh_manager.get(id);
        if (triangle_mesh == nullptr) {
            create_data(id);
            triangle_mesh = m_triangle_mesh_manager.get(id);
            ASSERT(triangle_mesh);
        }
        return triangle_mesh;
    }

    /**
     * @brief Get or create rendering geometry for given geometry data @p id.
     *
     * @note Unlike the triangle_mesh() method, this will always return valid pointer to geometry.
     *
     * @param id Geometry data identifier to get geometry for.
     * @return Render::Geometry instance pointer.
     */
    const Render::Geometry* geometry(GeometryDataId id)
    {
        auto geometry = m_geometry_manager.get(id);
        if (geometry == nullptr) {
            create_data(id);
            geometry = DEBUG_ASSERT_VAL(m_geometry_manager.get(id));
        }
        return geometry;
    }

private:

    void create_data(GeometryDataId id);
    void create_segment();
    void create_circle();
    void create_graded_circle();
    void create_smooth_sphere(double radius, double fa);
    indexed_triangle_set create_tool_marker();
    void create_candy_button();

private:
    Render::Device& m_device;
    MeshManager m_triangle_mesh_manager;
    GeometryManager m_geometry_manager;
};

} // namespace Slic3r::App::Scene
