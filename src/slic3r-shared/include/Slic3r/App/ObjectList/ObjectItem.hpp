#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"
#include "Slic3r/App/ObjectList/ExtruderMarkerReactor.hpp"
#include "Slic3r/App/ObjectList/VolumeItem.hpp"
#include "Slic3r/App/ObjectList/InstanceItem.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/ObjectList/ObjectObservableList.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
class ModelObject;
class ModelVolume;
struct ElementRef;
} // namespace Slic3r::Domain

namespace Slic3r::App {

namespace Yoga {
class Text;
class InputTextField;
} // namespace Yoga

class ExtruderIcon;
class ContextMenuRequester;

namespace ObjectList {

class ObjectButton : public TreeNodeButton
{
public:
    ObjectButton(CollapsibleArea* collapsible_item, bool is_printable);

    void set_name(const std::string& name);
    void set_printable(bool printable);
    void set_callbacks(
        Biz::ProjectInteractor& project_interactor,
        ContextMenuRequester& context_menu_requester,
        std::function<Biz::Scene::ObjectSelection()> get_selection
    );
    void set_selected(bool selected);
    void set_extruder_context(
        Biz::ProjectInteractor& project_interactor,
        std::optional<size_t> config_container_id,
        std::function<size_t()> get_object_id
    );
    void refresh_extruder(std::optional<size_t> config_container_id);

    /**
     * @brief Replaces the name label by the name editor until the editing is finished.
     */
    void start_renaming();

private:
    void hovered_updated_internal() override;
    void update_printable_visibility();
    void finish_renaming();

    Yoga::Text* m_name{nullptr};
    Yoga::InputTextField* m_name_editing{nullptr};
    ActionButton* m_printable_button{nullptr};
    ExtruderIcon* m_extruder_icon{nullptr};
    std::function<void(bool)> m_on_printable_toggled;
    ExtruderMarkerReactor m_extruder_marker_reactor;
    std::optional<size_t> m_config_container_id;
};

class ConnectorsButton : public LeafButton
{
public:
    ConnectorsButton();

    void set_selected(bool selected);
    void set_callbacks(
        Biz::ProjectInteractor& project_interactor,
        std::function<Biz::Scene::ObjectSelection()> get_selection
    );
};

class ObjectItem :
    public Biz::DataObserver<Biz::ObjectList::Object>,
    public Yoga::Item
{
public:
    ObjectItem(
        size_t index,
        const Biz::ObjectList::Object& state,
        Biz::ProjectInteractor& project_interactor,
        ContextMenuRequester& context_menu_requester
    );

    /**
     * @brief Shows the name editor on this object's row or on the row of its volume, if
     * @p element refers to this object.
     * @return true if a row of this object took it (the object is expanded for a volume).
     */
    bool start_renaming(const Domain::ElementRef& element);

private:
    void on_data_update() override;

    ObjectButton* m_object_button{nullptr};
    CollapsibleArea* m_collapsible_item{nullptr};
    ConnectorsButton* m_connectors_button{nullptr};
    VolumesItem* m_volumes{nullptr};
    InstancesItem* m_instances{nullptr};

    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace ObjectList
} // namespace Slic3r::App
