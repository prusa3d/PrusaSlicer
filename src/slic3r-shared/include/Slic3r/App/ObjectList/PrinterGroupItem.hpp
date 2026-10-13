#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

#include "Slic3r/App/ObjectList/ConfigContainerBedItem.hpp"

#include "Slic3r/Biz/ObjectList/ConfigContainerObservableList.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Domain {
class ConfigContainer;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Yoga {
class Icon;
class Text;
class Menu;
class MenuItem;
} // namespace Yoga

namespace ObjectList {

class ActionButton;

class ConfigContainerButton : public Yoga::RectangleButton
{
public:
    ConfigContainerButton(
        const std::string& name,
        std::function<void()>&& remove_fn,
        std::function<void()>&& duplicate_fn,
        std::function<void()>&& add_bed_fn
    );

    void set_name(const std::string& name);
    void set_enable_remove(bool enable);

private:
    Yoga::Icon* m_icon{nullptr};
    Yoga::Text* m_name{nullptr};
    ActionButton* m_add_bed_button{nullptr};

    // context menu for config containers
    Yoga::Menu* m_cc_context_menu{nullptr};
    Yoga::MenuItem* m_delete_cc_menu_item{nullptr};
};

class PrinterGroupItem : public Biz::DataObserver<Biz::ObjectList::PrinterGroup>, public Yoga::Item
{
public:
    PrinterGroupItem(
        size_t index,
        const Biz::ObjectList::PrinterGroup& state,
        Biz::ProjectInteractor& project_interactor,
        ContextMenuRequester& context_menu_requester,
        Mode mode
    );
    ~PrinterGroupItem() override;

    /**
     * @brief Pushes a bed thumbnail texture to the matching ConfigContainerBedItem, if this
     * group owns a bed with that id.
     * @return true if the bed was found (and set_thumbnail was called).
     */
    bool set_bed_thumbnail(size_t bed_id, Render::TexturePtr texture);

    /**
     * @brief Shows the name editor on the row of the object or volume @p element.
     * @return true if a row of this group took it.
     */
    bool start_renaming(const Domain::ElementRef& element);

private:
    void on_data_update() override;
    void remove_config_container();
    void duplicate_config_container();
    void add_bed();
    void remove_bed(size_t config_container_id, size_t bed_id);

    using BedListViewFactory = Yoga::ViewFactory<
        ConfigContainerBedItem,
        Biz::ObjectList::Bed,
        Biz::ProjectInteractor&,
        ContextMenuRequester&,
        RemoveBedFn,
        Mode>;
    using BedListView =
        Yoga::ListView<ConfigContainerBedItem, Biz::ObjectList::Bed, BedListViewFactory>;

    ConfigContainerButton* m_cc_button{nullptr};
    BedListView* m_beds_list_view{nullptr};

    Biz::ProjectInteractor& m_project_interactor;
    Mode m_mode;
};

} // namespace ObjectList
} // namespace Slic3r::App
