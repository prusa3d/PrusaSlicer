#pragma once

#include "Slic3r/App/Plater/MMPaintedVolumeRendering.hpp"
#include "Slic3r/App/Plater/SinkingContours.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"

namespace Slic3r::App::Plater {

class PlaterScenePresenterProjectContext : public Scene::ScenePresenterProjectContext
{
public:
    using MMPaintedGeometryManager = Render::GeometryManager<MMPainting::MMPaintedVolumeGeometryId>;

    PlaterScenePresenterProjectContext() = default;
    PlaterScenePresenterProjectContext(const PlaterScenePresenterProjectContext&) = delete;
    PlaterScenePresenterProjectContext& operator=(const PlaterScenePresenterProjectContext&) = delete;
    PlaterScenePresenterProjectContext(PlaterScenePresenterProjectContext&&) = default;

    SinkingContours& sinking_contours() { return m_sinking_contours; }
    const SinkingContours& sinking_contours() const { return m_sinking_contours; }
    void set_sinking_contours_highlight_enabled(bool enable) { m_sinking_contours.set_highlight_enabled(enable); }

    void set_selection_obb_node_as_dirty() { m_selection_obb_node.dirty = true; }
    void update_selection_obb_node(Render::Device& device, const Biz::ProjectInteractor& project_interactor);
    void set_selection_obb_visible(bool visible);

    const MMPaintedGeometryManager& mm_painted_geometry_manager() const;
    MMPaintedGeometryManager& mm_painted_geometry_manager();

    Scene::Node* cc_selection_node() const
    {
        return m_cc_selection_node;
    }

    void set_cc_selection_node(Scene::Node* node)
    {
        m_cc_selection_node = node;
    }

    Render::Geometry* cc_selection_geometry() const
    {
        return m_cc_selection_geometry.get();
    }

    void set_cc_selection_geometry(std::unique_ptr<Render::Geometry>&& geom)
    {
        m_cc_selection_geometry = std::move(geom);
    }
private:
    SinkingContours m_sinking_contours;
    struct SelectionOBBNode
    {
        Scene::Node* main_node{ nullptr };
        Scene::Node* selection_node{ nullptr };
        bool dirty{ true };
        bool visible{ true };
        Scene::Node* volume_nodes_parent{ nullptr };
    };
    SelectionOBBNode m_selection_obb_node;
    MMPaintedGeometryManager m_mm_painted_geometry_manager{"mm_painted_geometry"};
    Scene::Node* m_cc_selection_node{nullptr};
    std::unique_ptr<Render::Geometry> m_cc_selection_geometry;
};

} // namespace Slic3r::App::Plater
