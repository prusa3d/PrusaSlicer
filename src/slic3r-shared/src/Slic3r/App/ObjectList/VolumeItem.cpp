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

#include <fmt/format.h>

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
    LeafButton(NodeLevel::SubObject),
    m_project_interactor(project_interactor),
    m_drop_position(Biz::Scene::VolumeInsertPosition::Before)
{
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    set_object_name("VolumeButton");
    set_allow_overlap(true);
    set_draggable(true);
    set_droppable(true);

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

    // Volumes can be reordered only within the same object and the same volume type.
    const std::string dnd_key =
        fmt::format("Volume_{}_{}", volume.obj_id, static_cast<int>(volume.type));
    set_dnd_payload(DnDPayload{.type{dnd_key}, .data{{"volume_id", volume.vol_id}}});
    set_accepted_keys({dnd_key});
}

void VolumeButton::dnd_accepted_internal(const DnDPayload& dnd_payload)
{
    const size_t* dropped_volume_id = dnd_payload.get<size_t>("volume_id");
    if (!dropped_volume_id || !can_drop_volume(*dropped_volume_id, m_drop_position)) {
        return;
    }

    m_project_interactor.scene_interactor().reorder_volume(
        Domain::ElementRef(state()->obj_id, 0, *dropped_volume_id),
        Domain::ElementRef(state()->obj_id, 0, state()->vol_id),
        m_drop_position
    );
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::ReorderVolumes);
}

void VolumeButton::dnd_could_accept_changed_internal(bool could_accept)
{
    m_drop_hovered = could_accept;
    if (!could_accept) {
        m_drop_line.reset();
    }
}

void VolumeButton::render(const Vec2f& pos, const Vec2f& size)
{
    // Before the base render, which handles the drop - so the drop uses this frame's position.
    update_drop_line(pos, size);
    LeafButton::render(pos, size);
}

const std::optional<VolumeButton::DropLine>& VolumeButton::drop_line() const
{
    return m_drop_line;
}

bool VolumeButton::can_drop_volume(
    size_t volume_id,
    Biz::Scene::VolumeInsertPosition position
) const
{
    return m_project_interactor.scene_interactor().can_reorder_volume(
        Domain::ElementRef(state()->obj_id, 0, volume_id),
        Domain::ElementRef(state()->obj_id, 0, state()->vol_id),
        position
    );
}

void VolumeButton::update_drop_line(const Vec2f& pos, const Vec2f& size)
{
    m_drop_line.reset();
    if (!m_drop_hovered) {
        return;
    }

    const bool upper_half = ImGui::GetMousePos().y < pos.y() + size.y() / 2.f;
    m_drop_position       = upper_half ? Biz::Scene::VolumeInsertPosition::Before :
                                         Biz::Scene::VolumeInsertPosition::After;

    // No line where the drop would keep the order unchanged (e.g. over the dragged row itself).
    const std::optional<DnDPayload>& payload = dragged_payload();
    const size_t* dragged_volume_id = payload ? payload->get<size_t>("volume_id") : nullptr;
    if (!dragged_volume_id || !can_drop_volume(*dragged_volume_id, m_drop_position)) {
        return;
    }

    // On the boundary shared with the neighbouring row, starting at the content indent,
    // so the line is aligned with the volume icons.
    m_drop_line = DropLine{
        .start = Vec2f(pos.x() + content_padding().left, upper_half ? pos.y() : pos.y() + size.y()),
        .end_x = pos.x() + size.x()
    };
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

void VolumesItem::render(const Vec2f& pos, const Vec2f& size)
{
    Item::render(pos, size);

    // Rows of a collapsed list are not rendered, so their drop lines are not up to date.
    if (!m_volumes_list_view->is_visible()) {
        return;
    }

    for (size_t i = 0; i < m_volumes_list_view->list_item_count(); ++i) {
        if (const std::optional<VolumeButton::DropLine>& drop_line =
                m_volumes_list_view->item_at(i)->drop_line())
        {
            render_drop_line(*drop_line);
            return; // only the hovered row has a drop line
        }
    }
}

void VolumesItem::render_drop_line(const VolumeButton::DropLine& drop_line) const
{
    const float radius    = ImGui::GetFontSize() * 0.2f;
    const float thickness = radius * 0.6f;
    const ImU32 color     = m_theme->color_imgui(Platform::Color::AccentSecondary);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);
    const ImVec2 start = to_im(drop_line.start);
    draw_list->AddCircleFilled(start, radius, color);
    draw_list->AddLine(start, ImVec2(drop_line.end_x, start.y), color, thickness);
}

} // namespace Slic3r::App::ObjectList
