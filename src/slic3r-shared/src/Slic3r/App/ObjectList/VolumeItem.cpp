#include "Slic3r/App/ObjectList/VolumeItem.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"

#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "libslic3r/ExtruderCandidates.hpp"

namespace Slic3r::App::ObjectList {

using namespace Yoga;

VolumesButton::VolumesButton(CollapsibleArea* collapsible_item) :
    TreeNodeButton(NodeLevel::SubObject, collapsible_item)
{
    emplace_back<Text>(Biz::_u8L("Volumes"));
}

VolumeButton::VolumeButton(
    size_t index,
    const Biz::ObjectList::Volume& state,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::ObjectList::Volume>(index, state),
    LeafButton(NodeLevel::SubObject)
{
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    set_object_name("VolumeButton");
    set_allow_overlap(true);

    Paddings paddings = content_padding().source;
    paddings.top = paddings.bottom = 0;
    set_content_padding(paddings);

    m_icon = add_icon(this, Render::Icon::SolidPartVolume, 18_fpx);
    m_name = emplace_back<InputTextField>("VolumeName");
    m_name->set_color(Platform::Color::Transparent);
    m_name->set_flex_grow(1.f);
    m_name->callbacks().text_edited = [this, &scene_interactor]()
    {
        scene_interactor
            .edit_name({this->state()->obj_id, 0, this->state()->vol_id}, m_name->text());
    };

    auto get_selection = [this]() -> Biz::Scene::ObjectSelection
    {
        return {
            Biz::Scene::SelectionMode::Volume,
            {{this->state()->obj_id, 0, this->state()->vol_id}}
        };
    };
    m_name->callbacks().active_changed = [this, &project_interactor, get_selection](bool active)
    {
        if (active) {
            select_elements(project_interactor, get_selection());
        }
    };

    callbacks().action = [this, &project_interactor, get_selection]()
    { select_elements(project_interactor, get_selection()); };

    m_extruder_icon = add_extruder_icon(this);
    on_data_update();

    m_extruder_marker_reactor.bind(
        project_interactor,
        state.config_container_id,
        [this, &project_interactor]() -> std::set<unsigned>
        {
            const Domain::ConfigContainer* config_container =
                project_interactor.selected_project()
                    .find_config_container(*this->state()->config_container_id);
            const Domain::ModelObject* object =
                project_interactor.selected_project().find_object_by_id(this->state()->obj_id);
            const Domain::ModelVolume* volume =
                project_interactor.selected_project()
                    .find_volume_by_id(this->state()->obj_id, this->state()->vol_id);
            if (!config_container || !object || !volume) {
                return {};
            }

            const bool has_extruder_overrides =
                volume->volume_settings.overrides.get("extruder").has_value();
            const bool relevant =
                (volume->is_modifier() && has_extruder_overrides) || volume->is_model_part();
            if (!relevant) {
                return {};
            }

            Domain::Preset::SelectedPresetConfigPack config_pack_viewer(
                config_container->selected_preset()
            );
            return Biz::Slicing::get_volume_extruder_candidates(
                volume->volume_settings,
                object->object_settings,
                config_pack_viewer.get_print(),
                config_container->virtual_extruders()
            );
        },
        [this](const ExtruderMarkerReactor::Result& result)
        { refresh_extruder_icon(m_extruder_icon, result); }
    );
}

static Render::Icon icon(Domain::ModelVolumeType type, bool is_text, bool is_svg)
{
    if (is_text) {
        switch (type) {
        case Domain::ModelVolumeType::MODEL_PART:
            return Render::Icon::TextSolidPartVolume;
        case Domain::ModelVolumeType::NEGATIVE_VOLUME:
            return Render::Icon::TextNegativeVolume;
        case Domain::ModelVolumeType::PARAMETER_MODIFIER:
            return Render::Icon::TextModifierVolume;
        default:
            break;
        }
        return Render::Icon::None;
    }
    if (is_svg) {
        switch (type) {
        case Domain::ModelVolumeType::MODEL_PART:
            return Render::Icon::SvgSolidPartVolume;
        case Domain::ModelVolumeType::NEGATIVE_VOLUME:
            return Render::Icon::SvgNegativeVolume;
        case Domain::ModelVolumeType::PARAMETER_MODIFIER:
            return Render::Icon::SvgModifierVolume;
        default:
            break;
        }
        return Render::Icon::None;
    }

    switch (type) {
    case Domain::ModelVolumeType::MODEL_PART:
        return Render::Icon::SolidPartVolume;
    case Domain::ModelVolumeType::NEGATIVE_VOLUME:
        return Render::Icon::NegativeVolume;
    case Domain::ModelVolumeType::PARAMETER_MODIFIER:
        return Render::Icon::ModifierVolume;
    case Domain::ModelVolumeType::SUPPORT_BLOCKER:
        return Render::Icon::SupportBlocker;
    case Domain::ModelVolumeType::SUPPORT_ENFORCER:
        return Render::Icon::SupportModifier;
    default:
        return Render::Icon::None;
    }
}

void VolumeButton::on_data_update()
{
    const Biz::ObjectList::Volume& volume = *state();
    m_icon->set_icon(icon(volume.type, volume.is_text, volume.is_svg));
    m_name->set_text(volume.name.empty() ? Biz::_u8L("Volume") : volume.name);
    m_extruder_marker_reactor.set_config_container(state()->config_container_id);
    set_checked(volume.selected);
}

VolumesItem::VolumesItem(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor)
{
    set_orientation(Orientation::Vertical);

    m_volumes_list_view = emplace_back<VolumeListView>(VolumeListViewFactory(project_interactor));
    m_volumes_list_view->set_orientation(Orientation::Vertical);
    m_volumes_list_view->callbacks().has_content = [this]()
    { return m_volumes_list_view->list_item_count() > 0; };

    m_volumes_button = emplace<VolumesButton>(0, m_volumes_list_view);
}

bool VolumesItem::has_visible_content() const
{
    return m_volumes_list_view->list_item_count() > 1;
}

void VolumesItem::set_source_list(std::weak_ptr<Biz::ObjectList::VolumeObservableList> volumes_list)
{
    m_volumes_list_view->set_source_list(volumes_list);
    m_volumes_button->update_state();
    set_visible(has_visible_content());
}

} // namespace Slic3r::App::ObjectList
