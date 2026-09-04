#pragma once

#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

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
class PaintOnSeamsDialog;
class PlaterScenePresenter;

class PaintOnSeamsGizmo : public PaintOnGizmoBase
{
public:
    PaintOnSeamsGizmo() = delete;

    PaintOnSeamsGizmo(
        Render::Device& device,
        Scene::GeometryDataFactory& data_factory,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    ~PaintOnSeamsGizmo() override;

    Scene::ToolType type() const override;
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
    void on_clipping_of_view_changed(double value) override;

private:
    Yoga::Passthrough<PaintOnSeamsDialog> m_dialog;
};

} // namespace Slic3r::App::Plater
