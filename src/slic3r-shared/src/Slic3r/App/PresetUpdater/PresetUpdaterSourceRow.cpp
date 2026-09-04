#include "Slic3r/App/PresetUpdater/PresetUpdaterSourceRow.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Platform/IFileExplorerHandler.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <optional>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

using namespace PresetUpdaterRowLayout;

constexpr Unit k_description_min_width{60, Unit::Type::FigmaPixel};

std::string reveal_tooltip_text()
{
    // TRN Preset updater source row. Tooltip of the button opening the source folder.
    return Biz::_u8L("Show in folder");
}

Text* add_elided_text(Item* parent, const std::string& text)
{
    Text* item = parent->emplace_back<Text>(text);
    item->set_wrap_mode(Text::WrapMode::WrapElide);
    item->set_flex_grow(1);
    return item;
}

std::string summary_text(const PresetUpdater::SourceRowState& row)
{
    const PresetUpdater::SourceCounts& counts = row.counts;
    const int pending                         = counts.pending();
    if (pending == 0) {
        if (row.check_failed) {
            // TRN Preset updater source row. Nothing is known about this source.
            return Biz::_u8L("Could not be checked");
        }
        if (!row.skipped_vendors.empty()) {
            // TRN Preset updater source row. Some presets unusable, the rest offer nothing.
            return Biz::_u8L("Some presets skipped");
        }
        // TRN Preset updater source row. Nothing to install.
        return Biz::_u8L("Up to date");
    }

    if (counts.attention() > 0) {
        if (counts.required == 0) {
            // TRN Preset updater source row. {} counts vendors with an unlisted version.
            const std::string format = Biz::_L_PLURAL_u8("{} unknown version", "{} unknown versions", counts.attention());
            return fmt::format(fmt::runtime(format), counts.attention());
        }
        // TRN Preset updater source row. {} counts changes needed to make presets usable.
        const std::string format = Biz::_L_PLURAL_u8("{} required change", "{} required changes", counts.attention());
        return fmt::format(fmt::runtime(format), counts.attention());
    }

    if (counts.new_vendors == pending) {
        // TRN Preset updater source row. {} counts vendors not on this computer yet.
        const std::string format = Biz::_L_PLURAL_u8("{} new vendor", "{} new vendors", pending);
        return fmt::format(fmt::runtime(format), pending);
    }
    if (counts.updates == pending) {
        // TRN Preset updater source row. {} counts vendors with a newer version.
        const std::string format = Biz::_L_PLURAL_u8("{} update available", "{} updates available", pending);
        return fmt::format(fmt::runtime(format), pending);
    }
    // TRN Preset updater source row. {} counts changes of mixed kinds.
    const std::string format = Biz::_L_PLURAL_u8("{} change available", "{} changes available", pending);
    return fmt::format(fmt::runtime(format), pending);
}

bool shows_summary(const PresetUpdater::SourceRowState& row)
{
    return row.update_state == PresetUpdater::SourceRowState::UpdateState::UpToDate
        || row.update_state == PresetUpdater::SourceRowState::UpdateState::HasUpdates;
}

std::string status_text(const PresetUpdater::SourceRowState& row)
{
    if (row.update_state == PresetUpdater::SourceRowState::UpdateState::NotChecked) {
        // TRN Preset updater source row. This source is switched off.
        return Biz::_u8L("Not used");
    }
    return shows_summary(row) ? summary_text(row) : std::string();
}

std::optional<Render::Icon> status_icon(const PresetUpdater::SourceRowState& row)
{
    if (!row.skipped_vendors.empty() || row.check_failed) {
        return Render::Icon::ErrorMarker;
    }
    if (row.counts.attention() > 0) {
        return Render::Icon::WarningMarker;
    }
    if (row.counts.pending() == 0) {
        return Render::Icon::CheckMark;
    }
    return std::nullopt;
}

} // namespace

PresetUpdaterSourceRow::PresetUpdaterSourceRow(
    size_t index,
    const PresetUpdater::SourceRowState& data,
    PresetUpdater::PresetUpdaterController& controller
) :
    Biz::DataObserver<PresetUpdater::SourceRowState>(index, data),
    m_controller(controller)
{
    set_orientation(Orientation::Vertical);
    set_gap(2_fpx);
    set_flex_shrink(0);
    set_fill(IM_COL32_BLACK_TRANS);
    set_border_color(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    set_border_width(1);
    set_padding(Paddings{source_frame_padding_horizontal, source_frame_padding_vertical});

    build_header();

    m_vendor_container = emplace_back<Item>();
    m_vendor_container->set_orientation(Orientation::Vertical);
    m_vendor_container->set_flex_shrink(0);
    m_vendor_container->set_visible(false);

    m_vendor_separator = m_vendor_container->emplace_back<Separator>(Orientation::Horizontal);
    m_vendor_separator->set_margin(Margins{0_fpx, 4_fpx, 0_fpx, 4_fpx});

    m_vendor_list_view = m_vendor_container->emplace_back<VendorListView>(VendorFactory{controller});
    m_vendor_list_view->set_orientation(Orientation::Vertical);
    m_vendor_list_view->set_gap(2_fpx);
    m_vendor_list_view->set_flex_shrink(0);

    on_data_update();
}

void PresetUpdaterSourceRow::build_header()
{
    Item* header = emplace_back<Item>();
    header->set_orientation(Orientation::Horizontal);
    header->set_align_items(YGAlignCenter);
    header->set_gap(column_gap);
    header->set_height(source_row_height);
    header->set_flex_shrink(0);

    Item* lead_slot = header->emplace_back<Item>();
    lead_slot->set_orientation(Orientation::Horizontal);
    lead_slot->set_align_items(YGAlignCenter);
    lead_slot->set_gap(column_gap);
    lead_slot->set_width(lead_slot_width);
    lead_slot->set_flex_shrink(0);

    m_tick = lead_slot->emplace_back<ToggleButton>();
    m_tick->callbacks().checked_changed = [this](bool checked)
    {
        if (m_updating) {
            return;
        }
        m_controller.set_source_selected(state()->uuid, checked);
    };

    m_arrow = lead_slot->emplace_back<LayoutButton>(std::string(), Render::Icon::CaretRight);
    m_arrow->set_width(icon_button_size);
    m_arrow->set_height(icon_button_size);
    m_arrow->callbacks().action = [this]() { set_expanded(!m_expanded); };

    m_name = header->emplace_back<Text>(std::string(), Render::ImguiFontType::Bold);
    m_name->set_width(name_width);
    m_name->set_wrap_mode(Text::WrapMode::WrapElide);
    m_name->set_flex_shrink(0);

    m_info_badge = header->emplace_back<LayoutButton>(std::string(), Render::Icon::WarningMarker);
    m_info_badge->set_width(icon_button_size);
    m_info_badge->set_height(icon_button_size);
    m_info_badge->set_visible(false);

    m_description = header->emplace_back<Text>(std::string());
    m_description->set_wrap_mode(Text::WrapMode::WrapElide);
    m_description->set_flex_grow(1);
    m_description->set_flex_shrink(1);
    m_description->set_min_width(k_description_min_width);

    m_icon_slot = header->emplace_back<Item>();
    m_icon_slot->set_orientation(Orientation::Horizontal);
    m_icon_slot->set_align_items(YGAlignCenter);
    m_icon_slot->set_justify_content(YGJustifyFlexEnd);
    m_icon_slot->set_gap(4_fpx);
    m_icon_slot->set_width(icon_slot_width);
    m_icon_slot->set_flex_shrink(0);

    m_reveal_button = m_icon_slot->emplace_back<LayoutButton>(
        std::string(),
        Render::Icon::OpenFolder,
        reveal_tooltip_text()
    );
    m_reveal_button->set_width(icon_button_size);
    m_reveal_button->set_height(icon_button_size);
    m_reveal_button->callbacks().action = [this]()
    {
        AppServices::instance().file_explorer_handler().open_folder(state()->zip_path.parent_path());
    };

    // TRN Preset updater source row. Tooltip of the button removing a local source.
    const std::string remove_tooltip = Biz::_u8L("Remove source");
    m_remove_button = m_icon_slot->emplace_back<LayoutButton>(
        std::string(),
        Render::Icon::RemoveTick,
        remove_tooltip
    );
    m_remove_button->set_width(icon_button_size);
    m_remove_button->set_height(icon_button_size);
    m_remove_button->callbacks().action = [this]()
    { m_controller.remove_local_repository(state()->uuid); };

    build_status_cell(header);

    m_action_slot = header->emplace_back<StackLayout>();
    m_action_slot->set_width(action_slot_width);
    m_action_slot->set_flex_shrink(0);

    m_action_slot->emplace_back<Item>();

    // TRN Preset updater source row. Check not started yet, another one is running.
    add_elided_text(m_action_slot, Biz::_u8L("Waiting..."));

    // TRN Preset updater source row. Check running.
    add_elided_text(m_action_slot, Biz::_u8L("Checking..."));

    // TRN Preset updater source row. Install running.
    add_elided_text(m_action_slot, Biz::_u8L("Installing..."));

    // TRN Preset updater source row button. Applies every pending change of this source.
    m_update_all_button = m_action_slot->emplace_back<LayoutButton>(Biz::_u8L("Apply all"));
    apply_button_size(m_update_all_button);
    m_update_all_button->set_flex_grow(1);
    m_update_all_button->callbacks().action = [this]()
    { m_controller.update_source(state()->uuid); };
}

void PresetUpdaterSourceRow::build_status_cell(Item* header)
{
    Item* status = header->emplace_back<Item>();
    status->set_orientation(Orientation::Horizontal);
    status->set_align_items(YGAlignCenter);
    status->set_gap(4_fpx);
    status->set_width(status_width);
    status->set_flex_shrink(0);

    m_summary_icon = status->emplace_back<Icon>(Render::Icon::CheckMark);
    m_summary_icon->set_width(icon_size);
    m_summary_icon->set_height(icon_size);
    m_summary_icon->set_flex_shrink(0);
    m_summary_text = add_elided_text(status, std::string());
}

void PresetUpdaterSourceRow::set_expanded(bool expanded)
{
    m_expanded = expanded;
    m_vendor_container->set_visible(m_expanded);
    m_arrow->set_icon(m_expanded ? Render::Icon::CaretDown : Render::Icon::CaretRight);
}

void PresetUpdaterSourceRow::on_data_update()
{
    const PresetUpdater::SourceRowState* row = state();
    if (row == nullptr) {
        return;
    }

    const bool is_local = !row->zip_path.empty();

    m_updating = true;
    m_tick->set_checked(row->selected);
    m_updating = false;

    m_tick->set_enabled(!row->selection_locked);

    m_name->set_text(row->name);

    m_info_badge->set_visible(!row->visibility.empty());
    m_info_badge->set_tooltip(row->visibility);

    if (is_local) {
        m_description->set_text(row->zip_path.filename().string());
        m_reveal_button->set_tooltip(reveal_tooltip_text() + "\n" + row->zip_path.string());
    } else {
        m_description->set_text(row->description);
    }

    m_reveal_button->set_visible(is_local);
    m_remove_button->set_visible(is_local);
    m_remove_button->set_enabled(!row->install_locked);

    m_vendor_list_view->set_source_list(std::weak_ptr<PresetUpdater::VendorRowList>(row->vendors));

    const bool has_vendors = row->vendors->size() > 0;
    m_arrow->set_enabled(has_vendors);
    if (!has_vendors && m_expanded) {
        set_expanded(false);
    }

    m_summary_text->set_text(status_text(*row));

    const std::optional<Render::Icon> icon =
        shows_summary(*row) ? status_icon(*row) : std::nullopt;
    m_summary_icon->set_visible(icon.has_value());
    if (icon.has_value()) {
        m_summary_icon->set_icon(*icon);
        m_summary_icon->set_tint(
            *icon == Render::Icon::ErrorMarker ? m_theme->color_imgui(Platform::Color::Error) :
                                                 ImColor(1.0f, 1.0f, 1.0f)
        );
    }

    switch (row->update_state) {
    case PresetUpdater::SourceRowState::UpdateState::NotChecked:
        m_action_slot->set_current_index(ActionNone);
        break;
    case PresetUpdater::SourceRowState::UpdateState::Waiting:
        m_action_slot->set_current_index(ActionWaiting);
        break;
    case PresetUpdater::SourceRowState::UpdateState::Checking:
        m_action_slot->set_current_index(ActionChecking);
        break;
    case PresetUpdater::SourceRowState::UpdateState::Installing:
        m_action_slot->set_current_index(ActionInstalling);
        break;
    case PresetUpdater::SourceRowState::UpdateState::UpToDate:
        m_action_slot->set_current_index(ActionNone);
        break;
    case PresetUpdater::SourceRowState::UpdateState::HasUpdates:
        m_action_slot->set_current_index(row->install_locked ? ActionNone : ActionApplyAll);
        break;
    }
}

} // namespace Slic3r::App
