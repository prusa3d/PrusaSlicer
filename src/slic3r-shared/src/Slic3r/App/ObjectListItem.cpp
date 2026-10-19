#include "Slic3r/App/ObjectListItem.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/ProgressBar.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::App {

using namespace Yoga;

namespace ObjectList {

OutOfBedsItem::OutOfBedsItem(
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester
)
{
    set_object_name("OutOfBedsItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);

    Item* label_wrapper = emplace_back<Item>();
    label_wrapper->set_orientation(Orientation::Vertical);
    label_wrapper->set_padding(Paddings(20_fpx, 5_fpx));
    Text* label = label_wrapper->emplace_back<Text>(Biz::_u8L("Out of bed"));
    label->set_font_type(Render::ImguiFontType::Bold);

    m_objects_list_view = emplace_back<ObjectListView>(
        ObjectListViewFactory(project_interactor, context_menu_requester)
    );
    m_objects_list_view->set_object_name("OutOfBedObjectListView");
    m_objects_list_view->set_orientation(Orientation::Vertical);
}

OutOfBedsItem::~OutOfBedsItem()
{
    if (m_objects_list.is_valid()) {
        m_objects_list->remove_listener<Biz::IListObserver<Biz::ObjectList::Object>>(this);
    }
}

void OutOfBedsItem::set_source_list(
    std::weak_ptr<Biz::ObjectList::ObjectObservableList> out_of_bed_objects
)
{
    m_objects_list_view->set_source_list(out_of_bed_objects);

    if (m_objects_list.is_valid()) {
        m_objects_list->remove_listener<Biz::IListObserver<Biz::ObjectList::Object>>(this);
    }

    m_objects_list = out_of_bed_objects;
    m_objects_list->add_listener<Biz::IListObserver<Biz::ObjectList::Object>>(this);

    update_visibility();
}

bool OutOfBedsItem::start_renaming(const Domain::ElementRef& element)
{
    for (size_t i = 0; i < m_objects_list_view->list_item_count(); ++i) {
        if (m_objects_list_view->item_at(i)->start_renaming(element)) {
            return true;
        }
    }
    return false;
}

void OutOfBedsItem::update_visibility()
{
    set_visible(m_objects_list_view->list_item_count() > 0);
}

void OutOfBedsItem::on_inserted(const Biz::ObjectList::Object& data, size_t index)
{
    update_visibility();
}

void OutOfBedsItem::on_updated(const Biz::IndexRange& index_range)
{
    update_visibility();
}

void OutOfBedsItem::on_removed(const Biz::IndexRange& index_range)
{
    update_visibility();
}

void OutOfBedsItem::on_reset()
{
    update_visibility();
}

AllBedsItem::AllBedsItem(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor),
    m_status_listener_scope(project_interactor.status_cache(), *this)
{
    set_object_name("AllBedsItem");
    set_orientation(Orientation::Horizontal);
    set_align_items(YGAlignCenter);
    set_padding(Paddings(20_fpx, 5_fpx));
    set_gap(5_fpx);
    set_flex_shrink(0);

    Icon* icon = emplace_back<Icon>(Render::Icon::AllBeds);
    icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    icon->set_aspect_ratio(1);
    icon->set_width(18_fpx);

    Text* label = emplace_back<Text>(Biz::_u8L("All beds"));
    label->set_flex_grow(1.f);

    m_progress_bar = emplace_back<ProgressBar>();
    m_progress_bar->set_show_overlay(true);
    m_progress_bar->set_min_height(18_fpx);
    m_progress_bar->set_min_width(80_fpx);
    m_progress_bar->set_rounding(3.f);
    m_progress_bar->set_progress_rounding(3.f);
    m_progress_bar->set_overlay_color(m_theme->color_imgui(Platform::Color::Text));
    m_progress_bar->set_progress_fill(m_theme->color_imgui(Platform::Color::AccentSecondary));

    refresh();
}

void AllBedsItem::refresh()
{
    int total_beds{0};
    int finished_beds{0};
    for (const std::unique_ptr<Domain::ConfigContainer>& cc :
         m_project_interactor.scene_interactor().selected_project_config_containers())
    {
        for (const std::unique_ptr<Domain::BedInstance>& bed_inst : cc->bed_instances()) {
            if (bed_inst->model_instances.empty()) {
                continue;
            }
            total_beds++;
            const std::optional<Biz::Slicing::Status> status =
                m_project_interactor.status_cache()
                    .get_status({m_project_interactor.selected_project_id(), bed_inst->id().id});
            if (status && status->code == Biz::Slicing::StatusCode::Finished) {
                finished_beds++;
            }
        }
    }

    set_visible(total_beds > 1);
    if (total_beds > 0) {
        const int percent =
            total_beds == finished_beds ? 100 : int(100.0 * finished_beds / total_beds);
        m_progress_bar->set_progress(
            percent,
            fmt::format("{}/{} {}", finished_beds, total_beds, Biz::_u8L("SLICED"))
        );
    }
}

void AllBedsItem::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    refresh();
}

void AllBedsItem::on_status_cache_progress_changed(const Domain::SlicingId id)
{
    refresh();
}

} // namespace ObjectList

ObjectListItem::ObjectListItem(Biz::ProjectInteractor& project_interactor, Mode mode) :
    ScrollArea("ObjectListItem"),
    m_object_list_interactor(project_interactor.scene_interactor().object_list_interactor()),
    m_mode(mode),
    m_project_interactor(project_interactor),
    m_selected_project_listener_scope(project_interactor, *this)
{
    set_orientation(Yoga::Orientation::Vertical);
    set_min_height(100);
    set_padding(Paddings{0,0,0, 25_fpx});

    m_printer_group_list_view = emplace_back<PrinterGroupListView>(
        PrinterGroupListViewFactory(project_interactor, m_context_menu_requester, m_mode)
    );
    m_printer_group_list_view->set_object_name("PrinterGroups");
    m_printer_group_list_view->set_orientation(Yoga::Orientation::Vertical);
    m_printer_group_list_view->set_source_list(
        m_object_list_interactor.printer_groups(project_interactor.selected_project_id())
    );
    m_printer_group_list_view->set_flex_shrink(0);

    if (m_mode == Mode::Plater) {
        m_out_of_beds_item = emplace_back<ObjectList::OutOfBedsItem>(
            project_interactor,
            m_context_menu_requester
        );
        m_out_of_beds_item->set_source_list(
            m_object_list_interactor.out_of_bed_objects(project_interactor.selected_project_id())
        );
    } else {
        m_all_beds_item = emplace_back<ObjectList::AllBedsItem>(project_interactor);
    }
}

void ObjectListItem::on_selected_project_changed(size_t project_id)
{
    m_printer_group_list_view->set_source_list(m_object_list_interactor.printer_groups(project_id));
    if (m_mode == Mode::Plater) {
        m_out_of_beds_item->set_source_list(
            m_object_list_interactor.out_of_bed_objects(project_id)
        );
    }
}

void ObjectListItem::set_bed_instance_icons(const Plater::BedThumbnailTextures& icons)
{
    for (const Plater::BedThumbnailTexture& icon : icons) {
        for (size_t i = 0; i < m_printer_group_list_view->list_item_count(); ++i) {
            if (m_printer_group_list_view->item_at(i)
                    ->set_bed_thumbnail(icon.bed_instance_id, icon.thumbnail))
            {
                break;
            }
        }
    }
}

void ObjectListItem::add_context_menu_listener(IShowContextMenuListener* listener)
{
    m_context_menu_requester.add_listener<IShowContextMenuListener>(listener);
}

void ObjectListItem::start_renaming_selected()
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.empty()) {
        return;
    }

    // Object rows are identified by the object id only, volume rows also by the volume id.
    const Domain::ElementRef& selected = selection.elements.front();
    const Domain::ElementRef element = selection.mode == Biz::Scene::SelectionMode::Volume ?
        Domain::ElementRef(selected.object_id, 0, selected.volume_id) :
        Domain::ElementRef(selected.object_id);

    for (size_t i = 0; i < m_printer_group_list_view->list_item_count(); ++i) {
        if (m_printer_group_list_view->item_at(i)->start_renaming(element)) {
            return;
        }
    }
    if (m_out_of_beds_item) {
        m_out_of_beds_item->start_renaming(element);
    }
}

} // namespace Slic3r::App
