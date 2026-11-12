#pragma once

#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/GeneratedSupportPointsCache.hpp"

#include <memory>
#include <optional>

namespace Slic3r::App::Scene {
class Clipper;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::Biz {
class GeneratedSupportPointsRequest;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {
class PaintOnSupportsDialog;
class PlaterScenePresenter;

class PaintOnSupportsGizmo : public PaintOnGizmoBase
{
public:
    PaintOnSupportsGizmo() = delete;

    PaintOnSupportsGizmo(
        Render::Device& device,
        Scene::GeometryDataFactory& data_factory,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    ~PaintOnSupportsGizmo() override;

    Scene::ToolType type() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;

    void on_deactivated() override;
    void on_model_reloaded(Domain::SelectionId project_id) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

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
    void on_clipping_of_view_changed(double value) override;
    void on_painting_stroke_applied() override;

private:
    void select_facets_by_angle(float threshold_deg);
    void auto_generate_support_painting();
    void start_automatic_painting(Domain::SlicingId slicing_id, Domain::ObjectID model_object_id);

    void on_support_points_request_completed(
        std::optional<Biz::ObjectSupportPointsRef> support_points
    );

    bool apply_generated_support_points(Domain::SlicingId slicing_id);
    void finish_automatic_painting(bool applied_support_points);
    void reset_automatic_painting_state();
    void cancel_automatic_painting();

    void show_invalid_print_setup_warning(Domain::SlicingId slicing_id) const;
    void show_printable_object_required_warning() const;
    void show_object_not_on_bed_warning() const;

    std::optional<Domain::SlicingId> m_automatic_painting_slicing_id;
    std::unique_ptr<Biz::GeneratedSupportPointsRequest> m_support_points_request;

    Yoga::Passthrough<PaintOnSupportsDialog> m_dialog;
};

} // namespace Slic3r::App::Plater
