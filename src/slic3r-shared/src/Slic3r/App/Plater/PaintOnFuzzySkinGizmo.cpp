#include "Slic3r/App/Plater/PaintOnFuzzySkinGizmo.hpp"

#include "Slic3r/App/Plater/PaintOnFuzzySkinDialog.hpp"
#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

using Slic3r::Domain::FacetsAnnotationKind;

namespace Slic3r::App::Plater {

PaintOnFuzzySkinGizmo::PaintOnFuzzySkinGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    PaintOnGizmoBase(device, data_factory, project_interactor, scene_presenter)
{
    m_dialog = std::make_unique<PaintOnFuzzySkinDialog>();
    m_dialog->set_tool_type(m_tool_type);
    m_dialog->set_brush_type(m_cursor_type);
    m_dialog->set_brush_radius(m_cursor_radius);
    m_dialog->set_smart_fill_angle(m_smart_fill_angle);
    m_dialog->set_clipping_of_view_value(0.);
    m_dialog->set_split_triangles_value(m_triangle_splitting_enabled);

    m_dialog->callbacks().tool_type_changed = [this](const PaintOnGizmoBase::ToolType tool_type)
    { this->set_tool_type(tool_type); };

    m_dialog->callbacks().brush_shape_changed =
        [this](const Biz::Algorithms::TriangleSelector::CursorType cursor_type)
    { this->set_cursor_type(cursor_type); };

    m_dialog->callbacks().brush_radius_changed = [this](const double value)
    { m_cursor_radius = static_cast<float>(value); };

    m_dialog->callbacks().smart_fill_angle_changed = [this](const double value)
    { m_smart_fill_angle = static_cast<float>(value); };

    m_dialog->callbacks().split_triangles_value_changed = [this](const bool value)
    { m_triangle_splitting_enabled = value; };

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

PaintOnFuzzySkinGizmo::~PaintOnFuzzySkinGizmo() = default;

Scene::ToolType PaintOnFuzzySkinGizmo::type() const
{
    return Scene::ToolType::PaintOnFuzzySkinGizmo;
}

std::unique_ptr<GizmoWindow> PaintOnFuzzySkinGizmo::release_ui_window()
{
    return m_dialog.release();
}

FacetsAnnotationKind PaintOnFuzzySkinGizmo::get_facets_annotation_kind() const
{
    return FacetsAnnotationKind::FuzzySkin;
}

const Domain::FacetsAnnotation& PaintOnFuzzySkinGizmo::get_facets_annotation(
    const Domain::ModelVolume& model_volume
) const
{
    return model_volume.fuzzy_skin_facets;
}

bool PaintOnFuzzySkinGizmo::set_facets_annotation(
    Domain::ModelVolume& model_volume,
    const Biz::Algorithms::TriangleSelector& triangle_selector
) const
{
    const bool result{model_volume.fuzzy_skin_facets.set_data(triangle_selector.serialize())};
    m_project_interactor.undo_provider().take_snapshot(
        Biz::UndoSnapshotType::PaintOnFuzzySkinStroke
    );
    return result;
}

Domain::TriangleSelector::TriangleStateType
PaintOnFuzzySkinGizmo::get_left_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::FUZZY_SKIN;
}

Domain::TriangleSelector::TriangleStateType
PaintOnFuzzySkinGizmo::get_right_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::NONE;
}

void PaintOnFuzzySkinGizmo::on_cursor_radius_changed(float value)
{
    m_dialog->set_brush_radius(static_cast<double>(value));
}

void PaintOnFuzzySkinGizmo::on_smart_fill_angle_changed(float value)
{
    m_dialog->set_smart_fill_angle(static_cast<double>(value));
}

void PaintOnFuzzySkinGizmo::on_clipping_of_view_changed(double value)
{
    m_dialog->set_clipping_of_view_value(static_cast<double>(value));
}

} // namespace Slic3r::App::Plater
