#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"

#include "Slic3r/Biz/ObjectList/InstanceObservableList.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Yoga {
class Text;
} // namespace Yoga

namespace ObjectList {

class InstancesButton : public TreeNodeButton
{
public:
    InstancesButton(CollapsibleArea* collapsible_item);
    void update_counter(size_t instances_count);

private:
    Yoga::Text* m_counter{nullptr};
};

class InstanceButton : public Biz::DataObserver<Biz::ObjectList::Instance>, public LeafButton
{
public:
    InstanceButton(
        size_t index,
        const Biz::ObjectList::Instance& state,
        Biz::ProjectInteractor& project_interactor
    );

    void on_data_update() override;

private:
    void hovered_updated_internal() override;
    void update_printable_visibility();

    Yoga::Text* m_name{nullptr};
    ActionButton* m_printable_button{nullptr};
};

class InstancesItem : public Yoga::Item
{
public:
    InstancesItem(
        size_t object_id,
        Biz::ProjectInteractor& project_interactor
    );
    bool has_visible_content() const;

    void set_source_list(std::weak_ptr<Biz::ObjectList::InstanceObservableList> instance_list);

private:
    using InstanceListViewFactory =
        Yoga::ViewFactory<InstanceButton, Biz::ObjectList::Instance, Biz::ProjectInteractor&>;
    using InstanceListView = Yoga::ListView<
        InstanceButton,
        Biz::ObjectList::Instance,
        InstanceListViewFactory,
        CollapsibleArea>;

    InstancesButton* m_instances_button{nullptr};
    InstanceListView* m_instances_list_view{nullptr};

    size_t m_object_id;
    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace ObjectList
} // namespace Slic3r::App
