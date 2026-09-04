#include "Slic3r/App/Plater/PaintOnSeamsGizmo.hpp"

#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Plater/PaintOnSeamsDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

using Slic3r::Domain::FacetsAnnotationKind;

namespace Slic3r::App::Plater {

PaintOnSeamsGizmo::PaintOnSeamsGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    PaintOnGizmoBase(device, data_factory, project_interactor, scene_presenter)
{
    m_dialog = std::make_unique<PaintOnSeamsDialog>();
    m_dialog->set_brush_type(m_cursor_type);
    m_dialog->set_brush_radius(m_cursor_radius);
    m_dialog->set_clipping_of_view_value(0.);

    m_dialog->callbacks().brush_shape_changed =
        [this](const Biz::Algorithms::TriangleSelector::CursorType cursor_type)
    { this->set_cursor_type(cursor_type); };

    m_dialog->callbacks().brush_radius_changed = [this](const double value)
    { m_cursor_radius = static_cast<float>(value); };

    m_dialog->callbacks().clipping_of_view_value_changed = [this](double value)
    {
        m_clipping_plane_presenter.set_position_by_ratio(value, true);
        this->update_clipping_plane();
    };

    m_dialog->callbacks().clipping_of_view_reset_direction = [this]()
    {
        m_clipping_plane_presenter.set_position_by_ratio(-1, false);
        this->update_clipping_plane();
    };

    m_dialog->callbacks().painting_reset = [this]() { this->clear_all_paintings(); };
}

PaintOnSeamsGizmo::~PaintOnSeamsGizmo() = default;

Scene::ToolType PaintOnSeamsGizmo::type() const
{
    return Scene::ToolType::PaintOnSeamsGizmo;
}

std::unique_ptr<GizmoWindow> PaintOnSeamsGizmo::release_ui_window()
{
    return m_dialog.release();
}

FacetsAnnotationKind PaintOnSeamsGizmo::get_facets_annotation_kind() const
{
    return FacetsAnnotationKind::Seam;
}

const Domain::FacetsAnnotation& PaintOnSeamsGizmo::get_facets_annotation(
    const Domain::ModelVolume& model_volume
) const
{
    return model_volume.seam_facets;
}

bool PaintOnSeamsGizmo::set_facets_annotation(
    Domain::ModelVolume& model_volume,
    const Biz::Algorithms::TriangleSelector& triangle_selector
) const
{
    const bool result{model_volume.seam_facets.set_data(triangle_selector.serialize())};
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::PaintOnSeamsStroke);
    return result;
}

Domain::TriangleSelector::TriangleStateType PaintOnSeamsGizmo::get_left_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::ENFORCER;
}

Domain::TriangleSelector::TriangleStateType PaintOnSeamsGizmo::get_right_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::BLOCKER;
}

void PaintOnSeamsGizmo::on_cursor_radius_changed(float value)
{
    m_dialog->set_brush_radius(static_cast<double>(value));
}

void PaintOnSeamsGizmo::on_clipping_of_view_changed(double value)
{
    m_dialog->set_clipping_of_view_value(static_cast<double>(value));
}

} // namespace Slic3r::App::Plater
