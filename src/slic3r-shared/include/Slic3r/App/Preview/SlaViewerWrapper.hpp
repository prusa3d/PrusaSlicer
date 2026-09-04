#pragma once

#include "AbstractViewerWrapper.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Types.hpp"
#include "SlaViewerWrapperInputData.hpp"
#include "Slic3r/App/Imgui/DoubleSlider.hpp"

#include "Slic3r/App/libvgcode/SlaViewer.hpp"

namespace Slic3r::App::Preview {

class SlaViewerWrapper : public AbstractViewerWrapper
{
public:
    SlaViewerWrapper() = default;
    ~SlaViewerWrapper() override;

    bool init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory) override;
    bool set_settings(const ViewerWrapperBaseSettings& settings);

    void set_scene(Scene::Scene& scene) override
    {
        m_viewer.set_scene(scene);
    }

    void clear_scene() override
    {
        m_viewer.clear_scene();
    }

    void render_scene() override;
    void render_imgui() override;

    void reset() override;

    const libvgcode::AbstractViewer& viewer() const override {
        return *static_cast<const libvgcode::AbstractViewer*>(&m_viewer); }

    libvgcode::AbstractViewer& viewer() override {
        return *static_cast<libvgcode::AbstractViewer*>(&m_viewer); }

    //
    // Load data from the SLAResult structure.
    //
    void load_from_result(const Biz::Slicing::SLAResult& result, const Scene::Transform& bed_transform);
    //
    // Load data from the sliced Object.
    //
    void load_from_object(const Biz::Slicing::Sla::Object& object, const Scene::Transform& bed_transform);

    void reset_result();
    void reset_object(const Domain::ObjectID& object_id);

    const libvgcode::Interval& view_visible_range() const { return m_viewer.view_visible_range(); }
    const libvgcode::Interval& view_enabled_range() const { return m_viewer.view_enabled_range(); }

    const libvgcode::Interval& layers_range() const { return m_viewer.layers_range(); }
    void set_layers_range(libvgcode::Interval::value_type min, libvgcode::Interval::value_type max);

private:
    ViewerWrapperBaseSettings m_settings;
    SlaViewerWrapperInputData m_data;

    libvgcode::SlaViewer m_viewer;

    float m_legend_height{ 0.0f };

    bool m_loading{ false };

private:
    void update_slider_layers();
    void on_slider_layers_scroll_changed();
};

} // namespace Slic3r::App::Preview
