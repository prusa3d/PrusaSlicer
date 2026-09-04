#pragma once
#include <string>
#include <functional>
#include <set>
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp" // ISceneChangedListener
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"

namespace Slic3r::App::Plater {

/**
* @brief Manage notification about huge triangle meshes with suggestion to simplify it
*
* @note The notification lists meshes of the active project only, even though meshes of all
*       open projects are tracked.
*/
class SimplifyNotification final :
    public Biz::Scene::ISceneChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IProjectsChangedListener
{
public:
    SimplifyNotification(
        Biz::ProjectInteractor& project_interactor,
        Scene::GizmoManager& gizmo_manager,
        PopNotification::PopNotificationCenter& notify);

    ~SimplifyNotification() final;

    /**
     * @name Implementation of ISceneChangedListener interface
     * @note Listener is registred in constructor, Removed in destructor
     * @{
     * 
     * @brief Check need for simplification
     */
    void on_volume_added(Domain::SelectionId project_id, const Domain::ElementRefs& volumes) override;

    /**
     * @brief Update list of object need simplify.
     * Close notification if last one
     * Remove volume from list of huge meshes
     */
    void on_volume_removed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes) override;

    /**
     * @brief When add object, first instance is created.
     */
    void on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& instances) override;

    /**
     * @brief When last instance is removed object is deleted.
     */
    void on_instance_removed(Domain::SelectionId project_id, const Domain::ElementRefs& instances) override;

    /**@}*/

    /**
     * @brief Rebuild the notification for the newly active project.
     * @note Implementation of ISelectedProjectChangedListener interface
     */
    void on_selected_project_changed(size_t index) override;

    /**
     * @brief Drop meshes of the removed project, they can no longer be simplified.
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_will_be_removed(Domain::SelectionId project_id) override;

    void on_simplify(Domain::SelectionId project_id, const Domain::ElementRefs& simplified_volumes);

    /**
     * @brief Notify user that custom supports, seams and multimaterial painting
     *        were removed after simplifying the mesh.
     */
    void on_paint_removed_after_simplify();
    
    struct Item {
        Domain::SelectionId project_id;
        Domain::ElementRef element;
        std::string button_name;
        std::string list_text;
    };
    using Items = std::vector<Item>;
    using CreateSimplifyFn = std::function<std::function<bool()>(const Item&)>;
private:

    void recreate_notification(Domain::SelectionId project_id, bool open_when_closed = false);

    Items m_meshes_to_simplify;

    std::set<Domain::SelectionId> m_dismissed_projects;

    Biz::ProjectInteractor& m_project_interactor;
    PopNotification::PopNotificationCenter& m_notify;
    CreateSimplifyFn m_create_simplify_fn;
};

} // namespace Slic3r::App::Plater
