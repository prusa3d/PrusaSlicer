#include "Slic3r/App/Plater/ReferenceFramePicker.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include <magic_enum/magic_enum.hpp>

namespace Slic3r::App::Plater {
using App::Yoga::AbstractButton;
using App::Yoga::RadioButton;
using Biz::_u8L;
using Biz::Scene::ObjectSelection;
using App::Yoga::Text;

ReferenceFramePicker::ReferenceFramePicker(
    Biz::ProjectInteractor& project_interactor,
    Biz::Scene::SelectionReferenceFrame preferred_frame
) :
    m_project_interactor{project_interactor},
    m_projects{project_interactor},
    m_preferred_frame{preferred_frame}
{
    m_project_interactor.scene_interactor()
        .add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);

    set_orientation(Yoga::Orientation::Vertical);
    set_gap(10);

    Text* title{emplace_back<Text>(_u8L("Coordinates"))};
    title->set_font_type(Render::ImguiFontType::Bold);
    Item* row{emplace_back<Item>()};
    row->set_justify_content(YGJustify::YGJustifyFlexStart);
    row->set_gap(10);

    m_volume_radio_button = row->emplace_back<RadioButton>(_u8L("Part"));
    m_mode_buttons.insert_button(m_volume_radio_button);
    m_instance_radio_button = row->emplace_back<RadioButton>(_u8L("Object"));
    m_mode_buttons.insert_button(m_instance_radio_button);
    m_bed_radio_button = row->emplace_back<RadioButton>(_u8L("Bed"));
    m_mode_buttons.insert_button(m_bed_radio_button);

    using Frame = Biz::Scene::SelectionReferenceFrame;
    switch (m_preferred_frame) {
    case Frame::Volume:
        m_volume_radio_button->set_checked(true);
        break;
    case Frame::Instance:
        m_instance_radio_button->set_checked(true);
        break;
    case Frame::Bed:
        m_bed_radio_button->set_checked(true);
        break;
    default:
        PANIC("Unknown option");
    }

    m_mode_buttons.callbacks().checked_changed = [&](AbstractButton*, AbstractButton*)
    {
        if (!m_reloading) {
            m_preferred_frame = get_checked_frame();
            reload();
        }
    };
    reload();
}

ReferenceFramePicker::~ReferenceFramePicker() {
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

void ReferenceFramePicker::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection&
)
{
    reload(project_id);
}

void ReferenceFramePicker::on_scene_selection_transformed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection&
) {
    reload(project_id);
}

void ReferenceFramePicker::on_activated() {
    m_projects.selected().activated = true;
    reload(std::nullopt);
}

void ReferenceFramePicker::on_deactivated() {
    m_projects.selected().activated = false;
}

void
ReferenceFramePicker::reload(std::optional<Domain::SelectionId> project_id)
{
    using Biz::Scene::SelectionReferenceFrame;

    if (!m_projects.selected().activated) {
        return;
    }

    if (project_id && project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    Biz::Scene::SceneInteractor& scene_interactor{m_project_interactor.scene_interactor()};

    const std::set<SelectionReferenceFrame> reference_frame_options{
        scene_interactor.object_selection_reference_frame_options()
    };

    set_visible(reference_frame_options.size() > 1);
    m_volume_radio_button->set_visible(reference_frame_options.contains(SelectionReferenceFrame::Volume));
    m_instance_radio_button->set_visible(reference_frame_options.contains(SelectionReferenceFrame::Instance));
    m_bed_radio_button->set_visible(reference_frame_options.contains(SelectionReferenceFrame::Bed));

    scene_interactor.reload_object_selection_reference_frame(m_preferred_frame);

    const SelectionReferenceFrame used_reference_frame{
        scene_interactor.object_selection_reference_frame()
    };

    m_reloading = true;
    ScopeGuard scope_guard{[&](){m_reloading = false;}};
    if (used_reference_frame == SelectionReferenceFrame::Volume) {
        m_volume_radio_button->set_checked(true);
    }
    if (used_reference_frame == SelectionReferenceFrame::Instance) {
        m_instance_radio_button->set_checked(true);
    }
    if(used_reference_frame == SelectionReferenceFrame::Bed) {
        m_bed_radio_button->set_checked(true);
    }
}

Biz::Scene::SelectionReferenceFrame ReferenceFramePicker::get_checked_frame() const
{
    using Frame = Biz::Scene::SelectionReferenceFrame;
    if (m_volume_radio_button->checked()) {
        return Frame::Volume;
    } else if (m_instance_radio_button->checked()) {
        return Frame::Instance;
    } else {
        ASSERT(m_bed_radio_button->checked());
        return Frame::Bed;
    }
}
} // namespace Slic3r::App::Plater
