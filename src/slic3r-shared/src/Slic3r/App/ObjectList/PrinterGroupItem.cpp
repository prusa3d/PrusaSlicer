#include "Slic3r/App/ObjectList/PrinterGroupItem.hpp"
#include "Slic3r/App/ObjectList/ConfigContainerBedItem.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"

#include "Slic3r/Domain/ConfigContainer.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::ObjectList {

using namespace Yoga;

ConfigContainerButton::ConfigContainerButton(
    const std::string& name,
    std::function<void()>&& remove_fn,
    std::function<void()>&& duplicate_fn,
    std::function<void()>&& add_bed_fn
)
{
    set_flex_shrink(0);
    set_allow_overlap(true);
    set_content_justify_content(YGJustifyFlexStart);
    set_content_align_items(YGAlignCenter);
    set_background_color(Platform::Color::ButtonTransparent);
    set_rounding(0.f);

    set_content_padding(Paddings({20_fpx, 5_fpx}));
    set_content_gap(5_fpx);

    m_icon = emplace_back<Icon>(Render::Icon::ConfigContainer);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_icon->set_aspect_ratio(1);
    m_icon->set_width(16_fpx);

    m_name = emplace_back<Text>(name);
    m_name->set_flex_grow(1.f);
    m_name->set_wrap_mode(Text::WrapMode::WrapElide);
    m_name->set_font_type(Render::ImguiFontType::Bold);

    m_add_bed_button = emplace_back<ActionButton>(Render::Icon::AddBedIcon, Biz::_u8L("Add bed"));
    m_add_bed_button->callbacks().action = std::move(add_bed_fn);

    // Create context menu
    m_cc_context_menu = emplace_back<Menu>("cc_context_menu", Yoga::Position::Top);
    m_cc_context_menu->set_offset(-Unit(30_fpx).value);

    m_delete_cc_menu_item =
        m_cc_context_menu->append_item(Biz::_u8L("Delete"), Render::Icon::DeleteBtnIcon);
    m_delete_cc_menu_item->callbacks().action = std::move(remove_fn); // m_remove_fn;

    m_cc_context_menu->append_item(Biz::_u8L("Duplicate"), Render::Icon::CopyForGizmo)
        ->callbacks()
        .action = std::move(duplicate_fn);

    callbacks().secondary_action = [this]() { m_cc_context_menu->open(); };
}

void ConfigContainerButton::set_name(const std::string& name)
{
    m_name->set_text(name);
}

void ConfigContainerButton::set_enable_remove(bool enable)
{
    m_delete_cc_menu_item->set_enabled(enable);
}

PrinterGroupItem::PrinterGroupItem(
    size_t index,
    const Biz::ObjectList::PrinterGroup& state,
    Biz::ProjectInteractor& project_interactor,
    ContextMenuRequester& context_menu_requester,
    Mode mode
) :
    Biz::DataObserver<Biz::ObjectList::PrinterGroup>(index, state),
    m_project_interactor(project_interactor),
    m_mode(mode)
{
    set_object_name("PrinterGroupItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);

    m_cc_button = emplace_back<ConfigContainerButton>(
        state.name,
        [this]() { remove_config_container(); },
        [this]() { duplicate_config_container(); },
        [this]() { add_bed(); }
    );
    m_cc_button->set_object_name(
        fmt::format("ConfigContainer {} Button", state.config_container_id)
    );

    m_beds_list_view = emplace_back<BedListView>(BedListViewFactory(
        project_interactor,
        context_menu_requester,
        [this](size_t config_container_id, size_t bed_id)
        { remove_bed(config_container_id, bed_id); },
        m_mode
    ));
    m_beds_list_view->set_object_name("BedListViewFactory");
    m_beds_list_view->set_orientation(Orientation::Vertical);
    m_beds_list_view->set_source_list(state.beds_list.get());
}

PrinterGroupItem::~PrinterGroupItem()
{
    if (m_beds_list_view) {
        m_beds_list_view->set_source_list(nullptr, true);
    }
}

void PrinterGroupItem::on_data_update()
{
    m_cc_button->set_name(state()->name);
    m_cc_button->set_object_name(
        fmt::format("ConfigContainer {} Button", state()->config_container_id)
    );

    // Don't allow to delete last config container
    const size_t containers_cnt =
        m_project_interactor.scene_interactor().selected_project_config_containers().size();
    m_cc_button->set_enable_remove(containers_cnt > 1);

    m_beds_list_view->set_source_list(state()->beds_list.get());
}

void PrinterGroupItem::remove_config_container()
{
    m_project_interactor.remove_config_container(state()->config_container_id);
    m_project_interactor.undo_provider().take_snapshot(
        Biz::UndoSnapshotType::DeleteConfigContainer
    );
}

void PrinterGroupItem::duplicate_config_container()
{
    m_project_interactor.duplicate_config_container(state()->config_container_id);
    m_project_interactor.undo_provider().take_snapshot(
        Biz::UndoSnapshotType::DuplicateConfigContainer
    );
}

void PrinterGroupItem::add_bed()
{
    m_project_interactor.scene_interactor().add_bed_instance(state()->config_container_id);
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::AddBed);
}

void PrinterGroupItem::remove_bed(size_t config_container_id, size_t bed_id)
{
    const Domain::ConfigContainer* cc =
        m_project_interactor.selected_project().find_config_container(config_container_id);
    ASSERT(cc != nullptr);
    if (cc->bed_instances().size() == 1) {
        remove_config_container();
    } else {
        m_project_interactor.scene_interactor().remove_bed_instance(
            {.config_container_id = config_container_id, .instance_id = bed_id}
        );
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::DeleteBed);
    }
}

bool PrinterGroupItem::set_bed_thumbnail(size_t bed_id, Render::TexturePtr texture)
{
    for (size_t i = 0; i < m_beds_list_view->list_item_count(); ++i) {
        ConfigContainerBedItem* bed_item = m_beds_list_view->item_at(i);
        if (bed_item->bed_id() == bed_id) {
            bed_item->set_thumbnail(std::move(texture));
            return true;
        }
    }
    return false;
}

} // namespace Slic3r::App::ObjectList
