#include "Slic3r/App/ObjectList/ObjectItem.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "libslic3r/ExtruderCandidates.hpp"

namespace Slic3r::App::ObjectList {

using namespace Yoga;

/**
 * @brief All connector-volume ElementRefs of the given object (on its first instance),
 * matching ObjectListOld::render_connectors_node's selection payload.
 */
static Domain::ElementRefs
connector_element_refs(Biz::ProjectInteractor& project_interactor, size_t object_id)
{
    Domain::ElementRefs refs;
    const Domain::ModelObject* object =
        project_interactor.selected_project().find_object_by_id(object_id);
    if (!object || object->instances.empty()) {
        return refs;
    }
    const size_t instance_id = object->instances.front()->id().id;
    for (const Domain::ModelVolume* volume : object->volumes) {
        if (volume->is_cut_connector()) {
            refs.push_back({object_id, instance_id, volume->id().id});
        }
    }
    return refs;
}

ObjectButton::ObjectButton(CollapsibleArea* collapsible_item, bool is_printable) :
    TreeNodeButton(NodeLevel::Object, collapsible_item)
{
    set_object_name("ObjectButton");
    set_allow_overlap(true);

    Paddings paddings = content_padding().source;
    paddings.top = paddings.bottom = 0;
    set_content_padding(paddings);

    add_icon(this, Render::Icon::ObjectIcon);

    m_name = emplace_back<InputTextField>("Object name");
    m_name->set_flex_grow(1.f);
    m_name->set_color(Platform::Color::Transparent);

    Item* button_wrap = emplace_back<Item>();
    button_wrap->set_align_items(YGAlignCenter);
    button_wrap->set_min_width(20_fpx);
    button_wrap->set_min_height(20_fpx);
    button_wrap->set_flex_shrink(0);

    m_printable_button = add_printable_button(
        button_wrap,
        PrintableItem::Object,
        is_printable,
        [this](bool printable)
        {
            if (m_on_printable_toggled) {
                m_on_printable_toggled(printable);
            }
        },
        [this]() { update_printable_visibility(); }
    );

    Item* extruder_icon_wrapper = emplace_back<Item>();
    extruder_icon_wrapper->set_width(20_fpx);
    extruder_icon_wrapper->set_height(20_fpx);
    m_extruder_icon = add_extruder_icon(extruder_icon_wrapper);
}

void ObjectButton::set_name(const std::string& name)
{
    m_name->set_text(name);
}

void ObjectButton::set_printable(bool printable)
{
    m_printable_button->set_checked(printable);
}

void ObjectButton::set_callbacks(
    Biz::ProjectInteractor& project_interactor,
    std::function<Biz::Scene::ObjectSelection()> get_selection
)
{
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    m_on_printable_toggled = [&scene_interactor, get_selection, this](bool printable)
    { scene_interactor.set_printable(get_selection().elements.front(), printable); };

    m_name->callbacks().text_edited = [this, &scene_interactor, get_selection]()
    { scene_interactor.edit_name(get_selection().elements.front(), m_name->text()); };

    m_name->callbacks().active_changed = [this, &project_interactor, get_selection](bool active)
    {
        if (active) {
            select_elements(project_interactor, get_selection());
        }
    };

    callbacks().action = [&project_interactor, get_selection]()
    { select_elements(project_interactor, get_selection()); };

    m_name->callbacks().hovered_changed = [this](bool) { update_printable_visibility(); };
}

void ObjectButton::set_selected(bool selected)
{
    set_checked(selected);
}

void ObjectButton::set_extruder_context(
    Biz::ProjectInteractor& project_interactor,
    std::optional<size_t> config_container_id,
    std::function<size_t()> get_object_id
)
{
    m_config_container_id = config_container_id;
    m_extruder_marker_reactor.bind(
        project_interactor,
        config_container_id,
        [this, &project_interactor, get_object_id]() -> std::set<unsigned>
        {
            const Domain::ConfigContainer* config_container =
                project_interactor.selected_project().find_config_container(*m_config_container_id);
            const Domain::ModelObject* object =
                project_interactor.selected_project().find_object_by_id(get_object_id());
            if (!config_container || !object) {
                return {};
            }
            Domain::Preset::SelectedPresetConfigPack config_pack_viewer(
                config_container->selected_preset()
            );
            return Biz::Slicing::get_object_extruder_candidates(
                *object,
                config_pack_viewer,
                config_container->virtual_extruders()
            );
        },
        [this](const ExtruderMarkerReactor::Result& result)
        { refresh_extruder_icon(m_extruder_icon, result); }
    );
}

void ObjectButton::refresh_extruder(std::optional<size_t> config_container_id)
{
    m_config_container_id = config_container_id;
    m_extruder_marker_reactor.set_config_container(m_config_container_id);
}

void ObjectButton::hovered_updated_internal()
{
    TreeNodeButton::hovered_updated_internal();
    update_printable_visibility();
}

void ObjectButton::update_printable_visibility()
{
    m_printable_button->set_visible(
        !m_printable_button->checked()
        || m_printable_button->hovered()
        || m_name->hovered()
        || hovered()
    );
}

ConnectorsButton::ConnectorsButton() : LeafButton(NodeLevel::Object)
{
    set_object_name("ConnectorsButton");

    add_icon(this, Render::Icon::CutConnectors);

    Text* label = emplace_back<Text>(Biz::_u8L("Connectors"));
    label->set_flex_grow(1.f);
}

void ConnectorsButton::set_selected(bool selected)
{
    set_checked(selected);
}

void ConnectorsButton::set_callbacks(
    Biz::ProjectInteractor& project_interactor,
    std::function<Biz::Scene::ObjectSelection()> get_selection
)
{
    callbacks().action = [&project_interactor, get_selection]()
    { select_elements(project_interactor, get_selection()); };
}

ObjectItem::ObjectItem(
    size_t index,
    const Biz::ObjectList::Object& state,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::ObjectList::Object>(index, state),
    m_project_interactor(project_interactor)
{
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);

    m_collapsible_item = emplace_back<CollapsibleArea>();
    m_collapsible_item->set_orientation(Orientation::Vertical);
    m_collapsible_item->callbacks().has_content = [this]()
    {
        return m_connectors_button->is_visible()
            || m_volumes->has_visible_content()
            || m_instances->has_visible_content();
    };

    m_connectors_button = m_collapsible_item->emplace_back<ConnectorsButton>();
    m_connectors_button->set_callbacks(
        project_interactor,
        [this, &project_interactor]() -> Biz::Scene::ObjectSelection
        {
            return {
                Biz::Scene::SelectionMode::Volume,
                connector_element_refs(project_interactor, this->state()->id)
            };
        }
    );

    m_volumes   = m_collapsible_item->emplace_back<VolumesItem>(project_interactor);
    m_instances = m_collapsible_item->emplace_back<InstancesItem>(state.id, project_interactor);

    m_object_button = emplace<ObjectButton>(0, m_collapsible_item, state.printable);
    m_object_button->set_name(state.name);
    m_object_button->set_callbacks(
        project_interactor,
        [this]() -> Biz::Scene::ObjectSelection
        { return {Biz::Scene::SelectionMode::Instance, {Domain::ElementRef{this->state()->id}}}; }
    );
    m_object_button->set_extruder_context(
        project_interactor,
        state.config_container_id,
        [this]() { return this->state()->id; }
    );

    m_object_button->set_selected(state.selected);
    m_connectors_button->set_selected(state.connectors_selected);
}

void ObjectItem::on_data_update()
{
    m_object_button->set_name(state()->name);
    m_object_button->set_printable(state()->printable);
    m_object_button->set_selected(state()->selected);

    m_connectors_button->set_visible(state()->has_connectors);
    m_connectors_button->set_selected(state()->connectors_selected);

    m_volumes->set_source_list(state()->volumes_list.get());
    m_instances->set_source_list(state()->instances_list.get());

    m_object_button->update_state();
    m_object_button->refresh_extruder(state()->config_container_id);
}

} // namespace Slic3r::App::ObjectList
