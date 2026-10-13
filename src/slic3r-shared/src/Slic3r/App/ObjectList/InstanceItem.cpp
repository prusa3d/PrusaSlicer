#include "Slic3r/App/ObjectList/InstanceItem.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/ModelObject.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::ObjectList {

using namespace Yoga;

InstancesButton::InstancesButton(CollapsibleArea* collapsible_item) :
    TreeNodeButton(NodeLevel::SubObject, collapsible_item)
{
    set_object_name("InstancesButton");
    add_icon(this, Render::Icon::InstancesIcon);
    emplace_back<Text>(Biz::_u8L("Instances"));
    m_counter = emplace_back<Text>("None");
}

void InstancesButton::update_counter(size_t instances_count)
{
    m_counter->set_text(fmt::format("({})", instances_count));
    update_state();
}

InstanceButton::InstanceButton(
    size_t index,
    const Biz::ObjectList::Instance& state,
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester
) :
    Biz::DataObserver<Biz::ObjectList::Instance>(index, state),
    LeafButton(NodeLevel::SubObject)
{
    Biz::Scene::SceneInteractor& scene_interactor = project_interactor.scene_interactor();

    set_object_name("InstanceButton");
    set_allow_overlap(true);
    Paddings paddings = content_padding().source;
    paddings.right    = paddings.right + 25_fpx;
    set_content_padding(paddings);

    add_icon(this, Render::Icon::ObjectIcon);

    m_name = emplace_back<Text>(Biz::_u8L("Instance") + " " + std::to_string(state.id));
    m_name->set_flex_grow(1.f);

    m_printable_button = add_printable_button(
        this,
        PrintableItem::Instance,
        state.printable,
        [this, &scene_interactor](bool printable)
        {
            scene_interactor
                .set_printable({this->state()->object_id, this->state()->id}, printable);
        },
        [this]() { update_printable_visibility(); }
    );

    auto get_selection = [this]() -> Biz::Scene::ObjectSelection
    {
        return {
            Biz::Scene::SelectionMode::Instance,
            {{this->state()->object_id, this->state()->id}}
        };
    };
    callbacks().action = [&project_interactor, get_selection]()
    { select_elements(project_interactor, get_selection()); };
    callbacks().secondary_action =
        [this, &project_interactor, &context_menu_requester, get_selection]()
    {
        request_context_menu(
            project_interactor,
            context_menu_requester,
            checked(),
            get_selection()
        );
    };

    set_checked(state.selected);
}

void InstanceButton::on_data_update()
{
    m_name->set_text(Biz::_u8L("Instance") + " " + std::to_string(state()->ordinal_id));
    m_printable_button->set_checked(state()->printable);
    set_checked(state()->selected);
}

void InstanceButton::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    update_printable_visibility();
}

void InstanceButton::update_printable_visibility()
{
    m_printable_button->set_visible(
        !state()->printable || hovered() || m_printable_button->hovered()
    );
}

InstancesItem::InstancesItem(
    size_t object_id,
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester
) :
    m_object_id(object_id),
    m_project_interactor(project_interactor)
{
    set_orientation(Orientation::Vertical);

    m_instances_list_view = emplace_back<InstanceListView>(
        InstanceListViewFactory(project_interactor, context_menu_requester)
    );
    m_instances_list_view->set_orientation(Orientation::Vertical);
    m_instances_list_view->callbacks().has_content = [this]()
    { return m_instances_list_view->list_item_count() > 0; };

    m_instances_button = emplace<InstancesButton>(0, m_instances_list_view);
}

bool InstancesItem::has_visible_content() const
{
    Domain::ModelObject* object =
        m_project_interactor.selected_project().find_object_by_id(m_object_id);
    return m_instances_list_view->list_item_count() > 1 || (object && object->instances.size() > 1);
}

void
InstancesItem::set_source_list(std::weak_ptr<Biz::ObjectList::InstanceObservableList> instance_list)
{
    m_instances_list_view->set_source_list(instance_list);
    m_instances_button->update_counter(m_instances_list_view->list_item_count());
    set_visible(has_visible_content());
}

} // namespace Slic3r::App::ObjectList
