#pragma once

#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/IVirtualExtrudersChangedListener.hpp"

namespace Slic3r::App::Scene {
class Clipper;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {
class MultiMaterialPaintingDialog;
class PlaterScenePresenter;

class MultiMaterialPaintingGizmo :
    public PaintOnGizmoBase,
    public Biz::IColorsChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IVirtualExtrudersChangedListener
{
public:
    static constexpr float CursorRadiusMin = 0.1f;

    MultiMaterialPaintingGizmo() = delete;

    MultiMaterialPaintingGizmo(
        Render::Device& device,
        Scene::GeometryDataFactory& data_factory,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    ~MultiMaterialPaintingGizmo() override;

    float get_cursor_radius_min() const override;

    float get_cursor_edge_limit() const override;

    Scene::ToolType type() const override;
    bool enabled() const override;

    void register_commands(Platform::CommandRegistry& registry) override;

    std::unique_ptr<GizmoWindow> release_ui_window() override;

    Domain::TriangleSelector::TriangleStateType get_left_button_state_type() const override;
    Domain::TriangleSelector::TriangleStateType get_right_button_state_type() const override;

protected:
    Domain::FacetsAnnotationKind get_facets_annotation_kind() const override;
    const Domain::FacetsAnnotation& get_facets_annotation(
        const Domain::ModelVolume& model_volume
    ) const override;
    bool set_facets_annotation(
        Domain::ModelVolume& model_volume,
        const Biz::Algorithms::TriangleSelector& triangle_selector
    ) const override;

    void on_cursor_radius_changed(float value) override;
    void on_smart_fill_angle_changed(float value) override;
    void on_bucket_fill_angle_changed(float value) override;
    void on_height_range_z_range_changed(float value) override;
    void on_clipping_of_view_changed(double value) override;

    Domain::ColorRGBA get_cursor_sphere_left_button_color() const override;
    Domain::ColorRGBA get_cursor_sphere_right_button_color() const override;

    Domain::ColorRGBA create_default_painting_color(
        const Domain::ModelVolume& model_volume
    ) const override;
    PaintingPalette create_painting_colors() const override;

    void update_painting_dialog_tools();

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

private:
    Yoga::Passthrough<MultiMaterialPaintingDialog> m_dialog;

    size_t m_first_brush_color_idx  = 0;
    size_t m_second_brush_color_idx = 1;
};

} // namespace Slic3r::App::Plater
