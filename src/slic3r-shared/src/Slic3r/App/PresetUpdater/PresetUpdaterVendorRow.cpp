#include "Slic3r/App/PresetUpdater/PresetUpdaterVendorRow.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

using namespace PresetUpdaterRowLayout;

using Biz::PresetUpdater::VendorReconfigurationState;

std::string change_label(VendorReconfigurationState state)
{
    switch (state) {
    case VendorReconfigurationState::Update:
        // TRN Preset updater vendor row. Change kind, noun: a newer version exists.
        return Biz::_u8L("Update");
    case VendorReconfigurationState::NewVendor:
        // TRN Preset updater vendor row. Change kind, noun: not installed yet.
        return Biz::_u8L("New presets");
    case VendorReconfigurationState::ForcedUpdate:
        // TRN Preset updater vendor row. Change kind, noun: installed version too old.
        return Biz::_u8L("Required update");
    case VendorReconfigurationState::ForcedDowngrade:
        // TRN Preset updater vendor row. Change kind, noun: installed version too new.
        return Biz::_u8L("Required downgrade");
    case VendorReconfigurationState::NotInIndex:
        // TRN Preset updater vendor row. Change kind, noun: version not listed.
        return Biz::_u8L("Unknown version");
    case VendorReconfigurationState::RemoveVendor:
        // TRN Preset updater vendor row. Change kind, noun: no source can repair these.
        return Biz::_u8L("Unusable presets");
    }
    return {};
}

std::string action_label(VendorReconfigurationState state)
{
    switch (state) {
    case VendorReconfigurationState::Update:
    case VendorReconfigurationState::ForcedUpdate:
        // TRN Preset updater vendor row button. Installs a newer version.
        return Biz::_u8L("Update");
    case VendorReconfigurationState::NewVendor:
        // TRN Preset updater vendor row button. Installs presets not on this computer.
        return Biz::_u8L("Install");
    case VendorReconfigurationState::ForcedDowngrade:
        // TRN Preset updater vendor row button. Installs an older version.
        return Biz::_u8L("Downgrade");
    case VendorReconfigurationState::NotInIndex:
        // TRN Preset updater vendor row button. Installs a version the source lists.
        return Biz::_u8L("Repair");
    case VendorReconfigurationState::RemoveVendor:
        // TRN Preset updater vendor row button. Deletes the installed presets.
        return Biz::_u8L("Remove");
    }
    return {};
}

std::string done_label(VendorReconfigurationState state)
{
    if (state == VendorReconfigurationState::RemoveVendor) {
        // TRN Preset updater vendor row. The presets were deleted.
        return Biz::_u8L("Removed");
    }
    // TRN Preset updater vendor row. Install finished.
    return Biz::_u8L("Updated");
}

bool has_warning_icon(VendorReconfigurationState state)
{
    return state == VendorReconfigurationState::ForcedUpdate
        || state == VendorReconfigurationState::ForcedDowngrade
        || state == VendorReconfigurationState::NotInIndex
        || state == VendorReconfigurationState::RemoveVendor;
}

std::string version_label(const Slic3r::Semver& version)
{
    return version.valid() ? version.to_string() : "--";
}

/**
 * Fills its cell and elides. A NoWrap text publishes its full width as a minimum, which in the
 * action slot pushes whatever follows it out past the right edge of the slot.
 */
Text* add_elided_text(Item* parent, const std::string& text)
{
    Text* item = parent->emplace_back<Text>(text);
    item->set_wrap_mode(Text::WrapMode::WrapElide);
    item->set_flex_grow(1);
    return item;
}

} // namespace

namespace PresetUpdaterRowLayout {

void apply_button_size(LayoutButton* button)
{
    button->set_content_padding(button_padding);
    button->set_height(button_height);
}

} // namespace PresetUpdaterRowLayout

PresetUpdaterVendorRow::PresetUpdaterVendorRow(
    size_t index,
    const PresetUpdater::VendorRowState& data,
    PresetUpdater::PresetUpdaterController& controller
) :
    Biz::DataObserver<PresetUpdater::VendorRowState>(index, data),
    m_controller(controller)
{
    set_orientation(Orientation::Horizontal);
    set_align_items(YGAlignCenter);
    set_gap(column_gap);
    set_height(vendor_row_height);
    set_flex_shrink(0);

    Item* lead_slot = emplace_back<Item>();
    lead_slot->set_orientation(Orientation::Horizontal);
    lead_slot->set_align_items(YGAlignCenter);
    lead_slot->set_justify_content(YGJustifyFlexEnd);
    lead_slot->set_width(lead_slot_width);
    lead_slot->set_flex_shrink(0);

    m_state_icon = lead_slot->emplace_back<Icon>(Render::Icon::WarningMarker);
    m_state_icon->set_width(icon_size);
    m_state_icon->set_height(icon_size);
    m_state_icon->set_flex_shrink(0);
    m_state_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);

    Item* name_column = emplace_back<Item>();
    name_column->set_orientation(Orientation::Vertical);
    name_column->set_justify_content(YGJustifyCenter);
    name_column->set_min_width(name_width);
    name_column->set_flex_grow(1);
    name_column->set_flex_shrink(0);

    m_name = name_column->emplace_back<Text>(std::string(), Render::ImguiFontType::Bold);
    m_name->set_wrap_mode(Text::WrapMode::WrapElide);

    m_comment = name_column->emplace_back<Text>(std::string());
    m_comment->set_wrap_mode(Text::WrapMode::WrapElide);

    m_change = emplace_back<Text>(std::string());
    m_change->set_width(change_width);
    m_change->set_flex_shrink(0);
    m_change->set_wrap_mode(Text::WrapMode::WrapElide);

    m_skipped_text = emplace_back<Text>(std::string());
    m_skipped_text->set_wrap_mode(Text::WrapMode::WrapElide);
    m_skipped_text->set_flex_grow(1);
    m_skipped_text->set_visible(false);

    m_version_group = emplace_back<Item>();
    m_version_group->set_orientation(Orientation::Horizontal);
    m_version_group->set_align_items(YGAlignCenter);
    m_version_group->set_gap(column_gap);
    m_version_group->set_width(status_width);
    m_version_group->set_flex_shrink(0);

    m_current_version = m_version_group->emplace_back<Text>(std::string());
    m_current_version->set_flex_shrink(0);

    Text* arrow = m_version_group->emplace_back<Text>("->");
    arrow->set_flex_shrink(0);

    m_recommended_version = m_version_group->emplace_back<Text>(std::string());
    m_recommended_version->set_wrap_mode(Text::WrapMode::WrapElide);
    m_recommended_version->set_flex_grow(1);

    // TRN Preset updater vendor row button. Opens the release notes.
    m_changelog_button = emplace_back<LayoutButton>(Biz::_u8L("Changelog"));
    apply_button_size(m_changelog_button);
    m_changelog_button->set_visible(false);

    build_action_slot();

    on_data_update();
}

void PresetUpdaterVendorRow::build_action_slot()
{
    m_action = emplace_back<StackLayout>();
    m_action->set_width(action_slot_width);
    m_action->set_flex_shrink(0);

    m_action_button = m_action->emplace_back<LayoutButton>(std::string());
    apply_button_size(m_action_button);
    m_action_button->set_flex_grow(1);
    m_action_button->callbacks().action = [this]()
    {
        const PresetUpdater::VendorRowState* row = state();
        m_controller.update_vendor(row->repo_id, row->vendor_id);
    };

    // TRN Preset updater vendor row. Install not started yet, another one is running.
    add_elided_text(m_action, Biz::_u8L("Waiting..."));

    // TRN Preset updater vendor row. Install running.
    add_elided_text(m_action, Biz::_u8L("Installing..."));

    Item* done = m_action->emplace_back<Item>();
    done->set_flex_grow(1);
    done->set_orientation(Orientation::Horizontal);
    done->set_align_items(YGAlignCenter);
    done->set_gap(4_fpx);
    Icon* done_icon = done->emplace_back<Icon>(Render::Icon::CheckMark);
    done_icon->set_width(icon_size);
    done_icon->set_height(icon_size);
    done_icon->set_flex_shrink(0);
    m_done_text = add_elided_text(done, std::string());

    Item* failed = m_action->emplace_back<Item>();
    failed->set_flex_grow(1);
    failed->set_orientation(Orientation::Horizontal);
    failed->set_align_items(YGAlignCenter);
    failed->set_gap(4_fpx);
    Icon* failed_icon = failed->emplace_back<Icon>(Render::Icon::ErrorMarker);
    failed_icon->set_width(icon_size);
    failed_icon->set_height(icon_size);
    failed_icon->set_flex_shrink(0);
    m_failed_text = add_elided_text(failed, std::string());
    // TRN Preset updater vendor row button. Runs a failed install again.
    m_retry_button = failed->emplace_back<LayoutButton>(Biz::_u8L("Retry"));
    apply_button_size(m_retry_button);
    m_retry_button->set_flex_shrink(0);
    m_retry_button->callbacks().action = [this]()
    {
        const PresetUpdater::VendorRowState* row = state();
        m_controller.update_vendor(row->repo_id, row->vendor_id);
    };

    m_action->emplace_back<Item>();
}

void PresetUpdaterVendorRow::on_data_update()
{
    const PresetUpdater::VendorRowState* row = state();
    if (row == nullptr) {
        return;
    }

    m_state_icon->set_icon(row->skipped ? Render::Icon::ErrorMarker : Render::Icon::WarningMarker);
    m_state_icon->set_tint(
        row->skipped ? m_theme->color_imgui(Platform::Color::Error) : ImColor(1.0f, 1.0f, 1.0f)
    );
    m_state_icon->set_visible(row->skipped || has_warning_icon(row->state));

    m_name->set_text(row->vendor_id);

    m_change->set_visible(!row->skipped);
    m_version_group->set_visible(!row->skipped);
    m_action->set_visible(!row->skipped);
    m_skipped_text->set_visible(row->skipped);
    if (row->skipped) {
        // TRN Preset updater vendor row. Line under the vendor name.
        m_comment->set_text(Biz::_u8L("Unusable data"));
        m_comment->set_visible(true);
        // TRN Preset updater vendor row. Shown instead of the change kind.
        m_skipped_text->set_text(Biz::_u8L("Skipped"));
        return;
    }

    m_comment->set_text(row->comment);
    m_comment->set_visible(!row->comment.empty());

    m_change->set_text(change_label(row->state));
    m_current_version->set_text(version_label(row->current_version));
    m_recommended_version->set_text(version_label(row->recommended_version));
    m_recommended_version->set_font_type(
        row->recommended_version != row->current_version ? Render::ImguiFontType::Bold :
                                                           Render::ImguiFontType::Regular
    );

    m_action_button->set_label(action_label(row->state));
    m_done_text->set_text(done_label(row->state));
    m_failed_text->set_text(row->error_text);
    m_retry_button->set_tooltip(row->error_text);
    m_retry_button->set_visible(!row->install_locked);

    switch (row->install_state) {
    case PresetUpdater::InstallState::Idle:
        m_action->set_current_index(row->install_locked ? ActionNone : ActionIdle);
        break;
    case PresetUpdater::InstallState::Queued:
        m_action->set_current_index(ActionQueued);
        break;
    case PresetUpdater::InstallState::Running:
        m_action->set_current_index(ActionRunning);
        break;
    case PresetUpdater::InstallState::Done:
        m_action->set_current_index(ActionDone);
        break;
    case PresetUpdater::InstallState::Failed:
        m_action->set_current_index(ActionFailed);
        break;
    }
}

} // namespace Slic3r::App
