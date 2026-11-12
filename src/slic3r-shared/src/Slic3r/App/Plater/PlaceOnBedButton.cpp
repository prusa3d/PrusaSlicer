#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"

namespace Slic3r::App::Plater {
using Biz::_u8L;

PlaceOnBedButton::PlaceOnBedButton(Biz::ProjectInteractor& project_interactor) :
    Yoga::LayoutButton{_u8L("Place on bed")},
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_selected_project_listener_scope(m_project_interactor, *this),
    m_scene_selection_listener_scope(m_scene_interactor, *this)
{
    set_background_color(Platform::Color::AccentPrimary);
    set_label_font_type(Render::ImguiFontType::Bold);
    reload();
}

void PlaceOnBedButton::on_scene_selection_bounding_box_updated(
    Domain::SelectionId,
    const Biz::Scene::ObjectSelection&
)
{
    reload();
}

void PlaceOnBedButton::action_internal()
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_scene_interactor.selection_bounding_box()
    };

    if (!selection_bounding_box) {
        return;
    }

    Domain::SquareMatrix4d relative_transform_world{Domain::SquareMatrix4d::Identity()};
    relative_transform_world.col(3).z() = -selection_bounding_box->min_z();
    m_scene_interactor.transform_selection(relative_transform_world);
}

void PlaceOnBedButton::on_selected_project_changed_final(size_t)
{
    reload();
}

void PlaceOnBedButton::reload()
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_scene_interactor.selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }

    const bool is_floating = selection_bounding_box->is_floating();

    set_visible(is_floating);
    set_enabled(is_floating);
}

} // namespace Slic3r::App::Plater
