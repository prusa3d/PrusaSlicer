#pragma once

#include "AbstractViewer.hpp"
#include "SegmentTemplate.hpp"
#include "Slic3r/App/libvgcode/SlaObjectNodeTag.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/App/Scene/Transform.hpp"

namespace Slic3r::Domain {
class TriangleMesh;
}

struct indexed_triangle_set;

namespace Slic3r::Biz::Slicing::Sla {
struct Object;
}

namespace Slic3r::Biz::Slicing {
struct SLAResult;
}

namespace Slic3r::App::Scene {
class Node;
}

//#define USE_TEXTURE_BUFFER (1 && SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED)

namespace Slic3r::App::libvgcode {

struct GCodeInputData;

class SlaViewer : public AbstractViewer
{
    using ModelGeometryManager = Render::GeometryManager<Scene::AuxiliaryElementId>;
    using ModelTriangleMeshManager = Scene::TriangleMeshManager<Scene::AuxiliaryElementId>;
public:
    SlaViewer();
    ~SlaViewer() = default;
    SlaViewer(const SlaViewer&) = delete;
    SlaViewer(SlaViewer&&) = delete;
    SlaViewer& operator = (const SlaViewer&) = delete;
    SlaViewer& operator = (SlaViewer&&) = delete;

    /**
     * @brief Initialize rendering geometry
     *
     * @param device The current device.
     * @param scene The current scene.
     * @param data_factory The geometry factory.
     */
    void init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory) override;

    void set_scene(Scene::Scene& scene) override;
    void clear_scene() override;

    //
    // Reset all caches and free gpu memory.
    //
    void reset() override;

    void load(const Biz::Slicing::SLAResult& result, const Scene::Transform& bed_transform);
    void load_layers(const std::vector<float>& layers_zs, const std::vector<double>& layers_times);
    void load_object(const Biz::Slicing::Sla::Object& object, const Scene::Transform& bed_transform);

    void reset_layers();
    void reset_object(const Domain::ObjectID object_id);
    //
    // Render
    //
    void render() override;

    void set_layers_range(Interval::value_type min, Interval::value_type max) override;
    void set_view_visible_range(Interval::value_type min, Interval::value_type max) override;

    float estimated_time() const override;
    float estimated_time_at(size_t id) const override;
    std::vector<float> layers_estimated_times() const override;

private:

    ModelGeometryManager m_model_geometry_manager;
    ModelTriangleMeshManager m_model_triangle_mesh_manager;

    Scene::Node* m_main_node{nullptr};
    const Biz::Slicing::SLAResult* m_result{ nullptr };
    Scene::Transform m_bed_instance_transform{Scene::Transform::Identity()};

private:

    void build_instance_node(
        const Biz::Slicing::Sla::Object& sla_object, 
        size_t instance_id,
        const Domain::Transform3d& trafo,
        Scene::Node* parent_node);

    void build_if_needed(
        size_t object_id,
        size_t instance_id,
        SlaMeshType type,
        std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh,
        const Domain::Transform3d& trafo,
        Scene::Node* parent_node);

    void build_sla_object_mesh(
        size_t object_id,
        size_t instance_id,
        SlaMeshType type,
        std::shared_ptr<const Slic3r::Domain::TriangleMesh> mesh,
        const Domain::Transform3d& trafo,
        Scene::NodeBuilder& builder);

    void build_clipping_plane_node(SlaMeshType plane_type, Scene::NodeBuilder& builder);
    void update_clipping_plane(SlaMeshType plane_type, indexed_triangle_set& plane_its);

    void update_preview_range(size_t min_layer_id, size_t max_layer_id);

    void update_view_full_range() override;
};

} // namespace Slic3r::App::libvgcode
