#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectList/TreeNodeButton.hpp"
#include "Slic3r/App/ObjectList/ExtruderMarkerReactor.hpp"

#include "Slic3r/Biz/ObjectList/VolumeObservableList.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
enum class ModelVolumeType : int;
} // namespace Slic3r::Domain

namespace Slic3r::App {

namespace Yoga {
class Icon;
class InputTextField;
} // namespace Yoga

class ExtruderIcon;

namespace ObjectList {

class VolumesButton : public TreeNodeButton
{
public:
    VolumesButton(CollapsibleArea* collapsible_item);
};

class VolumeButton : public Biz::DataObserver<Biz::ObjectList::Volume>, public LeafButton
{
public:
    VolumeButton(
        size_t index,
        const Biz::ObjectList::Volume& state,
        Biz::ProjectInteractor& project_interactor
    );
    void on_data_update() override;

private:
    // type
    Yoga::Icon* m_icon{nullptr};
    Yoga::InputTextField* m_name{nullptr};
    ExtruderIcon* m_extruder_icon{nullptr};
    ExtruderMarkerReactor m_extruder_marker_reactor;
};

class VolumesItem : public Yoga::Item
{
public:
    VolumesItem(Biz::ProjectInteractor& project_interactor);
    bool has_visible_content() const;

    void set_source_list(std::weak_ptr<Biz::ObjectList::VolumeObservableList> volumes_list);

private:
    using VolumeListViewFactory = Yoga::ViewFactory<
        VolumeButton,
        Biz::ObjectList::Volume,
        Biz::ProjectInteractor&>;
    using VolumeListView = Yoga::
        ListView<VolumeButton, Biz::ObjectList::Volume, VolumeListViewFactory, CollapsibleArea>;

    VolumesButton* m_volumes_button{nullptr};
    VolumeListView* m_volumes_list_view{nullptr};

    size_t m_object_id;
    Biz::ProjectInteractor& m_project_interactor;
};

} // namespace ObjectList
} // namespace Slic3r::App
