#pragma once

#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/ClipperPresenterHelper.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"

namespace Slic3r::Domain {
class ModelObject;
class ModelInstance;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

class Scene;
class Node;

enum class BuildMeshesNodes
{
    Yes,
    No
};

class ClipperPresenter
{
    using ModelGeometryManager     = Render::GeometryManager<ClipperElement>;
    using ModelTriangleMeshManager = TriangleMeshManager<ClipperElement>;

public:
    ClipperPresenter() {};
    ClipperPresenter(Clipper* clipper, Render::Device* device, ISceneProvider* scene_provider);

    void activate(
        const Domain::ModelObject* selected_object,
        const Domain::ModelInstance* selected_instance,
        Node* parent_node,
        double sla_shift                           = 0.,
        BuildMeshesNodes should_build_meshes_nodes = BuildMeshesNodes::Yes
    );
    void deactivate(bool force_enabled_scene_nodes = true);
    void reset();

    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);
    void set_position_by_ratio(double pos, bool keep_normal);
    void set_limiting_plane(const Domain::Vec3d& plane_normal, double plane_offset);
    void update_clipper(
        const Domain::Vec3d& clp_normal,
        double clp_offset,
        double pos,
        bool force_reset_ignored
    );
    bool is_outside_of_cut_contour(const Domain::Vec3d& point) const;

    void set_clickable_plane(bool clickable);
    void set_enable_mesh(bool enable);
    void set_enable_plane(bool enable);
    void set_enable_contour(bool enable);
    void set_color_mesh(Slic3r::Domain::ColorRGBA color);
    void set_color_plane(Slic3r::Domain::ColorRGBA color);
    void set_color_contour(Slic3r::Domain::ColorRGBA color);

    void reset_ignored();
    void set_ignored(const std::vector<MeshClipperContourId>& ids);
    bool has_ignored() const;
    bool has_valid_contour() const;

    bool
    unproject_on_cut_plane(const Ray& ray, Domain::Vec3d& pos_world, bool respect_contours = true);

    const Clipper& clipper() const
    {
        return *m_clipper;
    }

private:
    void init_main_node(Node* parent_node);
    // build Mesh nodes from selected instance
    void build_meshes_nodes(const Domain::Transform3d& inst_trafo);
    // build Plane/Contour nodes from Clipper
    void build_non_mesh_node(
        ClipperElementType type,
        const indexed_triangle_set& its,
        size_t clipper_id,
        size_t island_id
    );
    // update all nodes (rescreate Plane/Contour nodes and update clip for Mesh nodes) from Clipper
    void update_nodes();

private:
    Clipper* m_clipper{nullptr};
    Render::Device* m_device{nullptr};
    ISceneProvider* m_scene_provider{nullptr};

    ModelGeometryManager m_model_geometry_manager{"clipper_presenter_geometry_manager"};
    ModelTriangleMeshManager m_model_triangle_mesh_manager{"clipper_presenter_geometry_manager"};

    std::vector<MeshClipperContourId> m_ignored_ids;

    Node* m_main_node{nullptr};

    Slic3r::Domain::ColorRGBA m_mesh_color{Slic3r::Domain::ColorRGBA::GRAY()};
    Slic3r::Domain::ColorRGBA m_plane_color{Slic3r::Domain::ColorRGBA::YELLOW()};
    Slic3r::Domain::ColorRGBA m_contour_color{Slic3r::Domain::ColorRGBA::WHITE()};

    bool m_mesh_enabled{true};
    bool m_plane_enabled{true};
    bool m_contour_enabled{true};

    Ray m_translation_ray;
    double m_start_t{0};

    int inst_counter{0};
};
} // namespace Slic3r::App::Scene
