#include "Slic3r/App/Plater/SimplifyNotification.hpp"

#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"

#include "fmt/format.h"

namespace Slic3r::App::Plater {
using namespace Slic3r;
using namespace Slic3r::Biz;

namespace {
bool exist_item(const SimplifyNotification::Item& item, const ProjectInteractor& project_interactor) {
    Domain::ElementRef e = item.element;
    const Domain::Project& project = project_interactor.project(item.project_id);
    const Domain::ModelVolume* volume = project.find_volume_by_id(e.object_id, e.volume_id);
    return volume != nullptr;
}

bool check_volume_exist(
    SimplifyNotification::Item& item, // fill result
    const ProjectInteractor& project_interactor, 
    const SimplifyNotification::Items& items) {

    if (!exist_item(item, project_interactor)) {
        // find first existing
        bool found = false;
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            if (exist_item(*it, project_interactor)) {
                item = *it;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
} // namespace

SimplifyNotification::SimplifyNotification(
    ProjectInteractor& project_interactor,
    Scene::GizmoManager& gizmo_manager, 
    PopNotification::PopNotificationCenter& notify) :
    m_project_interactor(project_interactor),
    m_notify(notify)
{
    m_project_interactor.scene_interactor().add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.add_listener<Biz::IProjectsChangedListener>(this);
    m_create_simplify_fn = [this, &gizmo_manager](const Item& item_)->std::function<bool()> {
        return [this, item_, &gizmo_manager]() -> bool {
            Item item = item_; // copy to remove const qualifier
            if (!check_volume_exist(item, m_project_interactor, m_meshes_to_simplify)) {
                return true; // can't opne simplify without volume
            }

            m_project_interactor.select_project(item.project_id);
            Biz::Scene::ObjectSelection selection{
                .mode = Biz::Scene::SelectionMode::Volume,
                .elements = {item.element}
            };
            m_project_interactor.scene_interactor().set_object_selection(selection);
            if (gizmo_manager.current_tool_type() != Scene::ToolType::Simplify) {
                gizmo_manager.activate_tool(Scene::ToolType::Simplify);
            }
            return true; // close popup
        };
    };
}

SimplifyNotification::~SimplifyNotification(){
    m_notify.observable_list().close_notifications_of_type(
        PopNotification::PopNotificationType::SimplifySuggestion);
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.remove_listener<Biz::IProjectsChangedListener>(this);
}

namespace {
bool need_simplify(const indexed_triangle_set& its) {
    return its.indices.size() > 1000000; // 1 milion triangles
}

std::string create_button_name(const Domain::ModelVolume& volume) {
    // TRN: Text of notification button - Simplify "object_name"
    return fmt::format(fmt::runtime(_u8L("Simplify \"{}\"")), volume.get_object()->name);
}

std::string create_list_text(const Domain::ModelVolume& volume) {
    std::string count = fmt::to_string(float(volume.mesh().its.indices.size() / size_t(100000)) / 10.f);
    return fmt::format("- {} ({}M {})",  volume.get_object()->name, count, 
        // TRN: In simplify notification text saying how many million triangles an object has.
        _u8L("triangles"));
}

using namespace Slic3r::App::PopNotification;

std::string create_message(const SimplifyNotification::Items& items)
{
    if (items.size() == 1) {
        return _u8L(
            "This mesh exceeds 1M triangles and may cause processing slowdowns."
        );
    }
    std::string message =
        _u8L(
            "These meshes exceed 1M triangles and may cause processing slowdowns."
        ) + "\n";
    for (const SimplifyNotification::Item& item : items) {
        message += "\n" + item.list_text;
    }
    return message;
}

namespace {
bool close_notification(PopNotificationCenter& notify) {
    auto& list = notify.observable_list();
    bool is_open = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list.at(i).type == PopNotificationType::SimplifySuggestion) {
            is_open = true;
            break;
        }
    }
    if (!is_open) {
        return false;
    }

    list.close_notifications_of_type(
        PopNotificationType::SimplifySuggestion);
    return true;

}
} // namespace

SimplifyNotification::Items filter_by_project(
    const SimplifyNotification::Items& items,
    Domain::SelectionId project_id)
{
    SimplifyNotification::Items result;
    for (const SimplifyNotification::Item& item : items) {
        if (item.project_id == project_id) {
            result.push_back(item);
        }
    }
    return result;
}

} // namespace

void SimplifyNotification::recreate_notification(
    Domain::SelectionId project_id, bool open_when_closed)
{
    // Meshes of all open projects are tracked, but only the active project's belong in the
    // notification - the others are not visible to simplify.
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }
    if (m_dismissed_projects.contains(project_id)) {
        // user closed the notification for this project, only refresh it when still open
        open_when_closed = false;
    }
    const Items project_items = filter_by_project(m_meshes_to_simplify, project_id);

    if (bool was_open = close_notification(m_notify);
        (!open_when_closed && !was_open) || project_items.empty()) {
        return;
    }

    const Item& item = project_items.back();
    PopNotificationData data{
        .type = PopNotificationType::SimplifySuggestion,
        .level = PopNotificationLevel::Regular,
        .timeout = 0s,
        .layout = PopNotificationLayoutHeaderTextButtons {
            .header = _u8L("Reduce Triangle Count"),
            .text = create_message(project_items),
            .buttons = { PopNotificationButtonData {
                .text = item.button_name,
                .callback = m_create_simplify_fn(item)
            }}
        },
        .project_id = project_id,
        .on_user_close = [this, project_id]() { m_dismissed_projects.insert(project_id); }
    };
    auto matcher = [](const PopNotificationPayload&, const PopNotificationPayload&) { return false; };
    m_notify.upsert_notification(data, matcher);
}

void SimplifyNotification::on_volume_added(
    Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    bool exist_change = false;
    const Domain::Project& project = m_project_interactor.project(project_id);
    for (const Domain::ElementRef& element : volumes) {
        const Domain::ModelVolume* volume = 
            project.find_volume_by_id(element.object_id, element.volume_id);
        if (volume == nullptr || !need_simplify(volume->mesh().its)) {
            continue;
        }
        m_meshes_to_simplify.push_back(Item{
            .project_id = project_id,
            .element = element,
            .button_name = create_button_name(*volume),
            .list_text = create_list_text(*volume)
        });
        exist_change = true;
    }
    if (exist_change) {
        // a newly added oversized mesh is worth showing even when the user closed the
        // notification before - and it is shown together with the already listed ones
        m_dismissed_projects.erase(project_id);
        recreate_notification(project_id, /*open_when_closed=*/true);
    }
}

void SimplifyNotification::on_volume_removed(
    Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    bool exist_change = false;
    for (const Domain::ElementRef& el : volumes) {
        const auto [first, last] = std::ranges::remove_if(
            m_meshes_to_simplify, [project_id, &el](const Item& it) {
                return it.project_id == project_id && it.element == el; });
        if (first == last) {
            continue;
        }
        m_meshes_to_simplify.erase(first, last);
        exist_change = true;
    }
    if (exist_change) {
        recreate_notification(project_id);
    }
}

void SimplifyNotification::on_instance_added(
    Domain::SelectionId project_id, const Domain::ElementRefs& instances)
{
    Domain::ElementRefs el_volumes;
    const Domain::Project& project = m_project_interactor.project(project_id);
    for (const Domain::ElementRef& el_instance : instances) {
        const Domain::ModelObject* object = project.find_object_by_id(el_instance.object_id);
        if (object == nullptr || object->instances.size() != 1) {
            continue; // no first instance
        }

        // collect volumes of the just added object
        el_volumes.reserve(el_volumes.size() + object->volumes.size());
        for (const Domain::ModelVolume* volume : object->volumes) {
            el_volumes.emplace_back(el_instance.object_id, el_instance.instance_id, volume->id().id);
        }
    }
    if (el_volumes.empty()) {
        return; // no added volume
    }

    on_volume_added(project_id, el_volumes);
}

void SimplifyNotification::on_instance_removed(
    Domain::SelectionId project_id, const Domain::ElementRefs& instances) 
{
    bool exist_change = false;
    const Domain::Project& project = m_project_interactor.project(project_id);
    for (const Domain::ElementRef& el_instance : instances) {
        if (project.find_object_by_id(el_instance.object_id) != nullptr ) {
            continue; // not last instance
        }

        // find items from object
        const auto [first, last] = std::ranges::remove_if(m_meshes_to_simplify, 
            [project_id, object_id = el_instance.object_id](const Item& it) { 
                return it.project_id == project_id && it.element.object_id == object_id;
            });
        if (first == last) {
            continue; // no object volume in list
        }
        m_meshes_to_simplify.erase(first, last);
        exist_change = true;
    }
    if (exist_change) {
        recreate_notification(project_id);
    }
}

void SimplifyNotification::on_simplify(
    Domain::SelectionId project_id, 
    const Domain::ElementRefs& simplified_volumes)
{
    bool exist_change = false;
    for (const Domain::ElementRef& el : simplified_volumes) {
        const auto [first, last] = std::ranges::remove_if(m_meshes_to_simplify,
            [project_id, &el](const Item& it) {
                return it.project_id == project_id && 
                    it.element.object_id == el.object_id && 
                    it.element.volume_id == el.volume_id;
            });
        if (first == last) {
            continue; // no object volume in list
        }
        m_meshes_to_simplify.erase(first, last);
        exist_change = true;
    }
    if (exist_change) {
        recreate_notification(project_id);
    }

    // after simplify apply is assumption that you do not want to simplify more
    // even when model has more than 1M triangles. Soo do not check result of simplificaiton.
}

void SimplifyNotification::on_selected_project_changed(size_t index)
{
    recreate_notification(index, /*open_when_closed=*/true);
}

void SimplifyNotification::on_project_will_be_removed(Domain::SelectionId project_id)
{
    // project ids are never reused, so this is only to keep the set from growing
    m_dismissed_projects.erase(project_id);

    const auto [first, last] = std::ranges::remove_if(
        m_meshes_to_simplify,
        [project_id](const Item& it) { return it.project_id == project_id; });
    if (first == last) {
        return; // no mesh of the removed project in list
    }
    m_meshes_to_simplify.erase(first, last);
    recreate_notification(m_project_interactor.selected_project_id());
}

void SimplifyNotification::on_paint_removed_after_simplify()
{
    m_notify.upsert_notification(
        PopNotificationData{
            PopNotificationType::SimplifySuggestion,
            PopNotificationLevel::Warning,
            0s,
            PopNotificationLayoutText(_u8L(
                "Custom supports, seams, fuzzy skin and multimaterial painting were removed after simplifying the mesh."
            )),
            {},
            m_project_interactor.selected_project_id()
        },
        never_equal_matcher
    );
}

} // namespace Slic3r::App::Plater
