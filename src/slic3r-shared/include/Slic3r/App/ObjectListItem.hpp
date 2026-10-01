#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

#include "Slic3r/App/ObjectList/PrinterGroupItem.hpp"

#include "Slic3r/Biz/ObjectList/ConfigContainerObservableList.hpp"
#include "Slic3r/Biz/ObjectList/ObjectListInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/App/Plater/BedThumbnailTexture.hpp"
#include "Yoga/ScrollArea.hpp"

namespace Slic3r::Domain {
class ConfigContainer;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

namespace Yoga {
class ScrollArea;
class Text;
class ProgressBar;
} // namespace Yoga

namespace ObjectList {

/**
 * @brief Plater-mode-only section listing objects whose instances are not placed on any bed,
 * mirroring ObjectListOld::render_out_of_beds. Reuses ObjectObservableList/ObjectItem with the
 * bed_id == 0 "out of bed" sentinel.
 */
class OutOfBedsItem : public Yoga::Item, public Biz::IListObserver<Biz::ObjectList::Object>
{
public:
    OutOfBedsItem(Biz::ProjectInteractor& project_interactor);
    ~OutOfBedsItem();

    void set_source_list(std::weak_ptr<Biz::ObjectList::ObjectObservableList> out_of_bed_objects);

private:
    void update_visibility();
    void on_updated(const Biz::IndexRange& index_range) override;
    void on_removed(const Biz::IndexRange& index_range) override;

    using ObjectListViewFactory = Yoga::ViewFactory<
        ObjectItem,
        Biz::ObjectList::Object,
        Biz::ProjectInteractor&>;
    using ObjectListView =
        Yoga::ListView<ObjectItem, Biz::ObjectList::Object, ObjectListViewFactory>;

    ObjectListView* m_objects_list_view{nullptr};
    Biz::WeakerPointer<Biz::ObjectList::ObjectObservableList> m_objects_list{nullptr};
};

/**
 * @brief Preview-mode-only summary row showing overall slicing progress across all beds,
 * mirroring ObjectListOld::render_all_beds_node. Shown only when there is more than one
 * occupied bed.
 */
class AllBedsItem : public Yoga::Item, public Biz::IStatusCacheChangedListener
{
public:
    AllBedsItem(Biz::ProjectInteractor& project_interactor);

    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;
    void on_status_cache_progress_changed(const Domain::SlicingId id) override;

private:
    void refresh();

    Yoga::ProgressBar* m_progress_bar{nullptr};

    Biz::ProjectInteractor& m_project_interactor;
    Biz::ListenerScope<Biz::IStatusCacheChangedListener, Biz::StatusCache, AllBedsItem>
        m_status_listener_scope;
};

} // namespace ObjectList

class ObjectListItem : public Yoga::ScrollArea, public Biz::ISelectedProjectChangedListener
{
public:
    using Mode = ObjectList::Mode;

    ObjectListItem(Biz::ProjectInteractor& project_interactor, Mode mode);

    void set_bed_instance_icons(const Plater::BedThumbnailTextures& icons);

    void on_selected_project_changed(size_t index) override;

private:
    using PrinterGroupListViewFactory = Yoga::ViewFactory<
        ObjectList::PrinterGroupItem,
        Biz::ObjectList::PrinterGroup,
        Biz::ProjectInteractor&,
        Mode>;
    using PrinterGroupListView = Yoga::ListView<
        ObjectList::PrinterGroupItem,
        Biz::ObjectList::PrinterGroup,
        PrinterGroupListViewFactory>;

    PrinterGroupListView* m_printer_group_list_view{nullptr};
    ObjectList::OutOfBedsItem* m_out_of_beds_item{nullptr};
    ObjectList::AllBedsItem* m_all_beds_item{nullptr};

    Biz::ObjectList::ObjectListInteractor& m_object_list_interactor;

    Mode m_mode;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::ListenerScope<Biz::ISelectedProjectChangedListener, Biz::ProjectInteractor, ObjectListItem>
        m_selected_project_listener_scope;
};

} // namespace Slic3r::App
