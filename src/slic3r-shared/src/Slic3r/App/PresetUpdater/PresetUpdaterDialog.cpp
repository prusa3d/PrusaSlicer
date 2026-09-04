#include "Slic3r/App/PresetUpdater/PresetUpdaterDialog.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/OpenBrowser.hpp"
#include "Slic3r/App/Platform/IFileExplorerHandler.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

constexpr Unit k_preferred_max_width{900, Unit::Type::FigmaPixel};
constexpr Unit k_preferred_max_height{700, Unit::Type::FigmaPixel};

constexpr float k_viewport_fraction{0.9f};

constexpr float k_top_row_height{40};

constexpr const char* k_preset_repo_url{"https://help.prusa3d.com/slicer-profiles/3"};

void append_words(Item* row, std::string_view text)
{
    for (auto&& word_range : text | std::views::split(' ')) {
        if (word_range.begin() == word_range.end()) {
            continue;
        }
        std::string word(word_range.begin(), word_range.end());
        word += " ";
        row->emplace_back<Text>(word);
    }
}

std::string apply_everything_label()
{
    // TRN Preset updater dialog button. Applies every pending change of every source.
    return Biz::_u8L("Apply everything");
}

std::string apply_required_label()
{
    // TRN Preset updater dialog button. Applies only the required changes.
    return Biz::_u8L("Apply everything required");
}

std::string up_to_date_label()
{
    // TRN Preset updater dialog button, disabled. Nothing to apply.
    return Biz::_u8L("Everything is up to date");
}

bool has_forced_vendor(const PresetUpdater::SourceRowState& source)
{
    for (size_t index = 0; index < source.vendors->size(); ++index) {
        const PresetUpdater::VendorRowState& vendor = source.vendors->at(index);
        if (vendor.skipped) {
            continue;
        }
        if (vendor.state == Biz::PresetUpdater::VendorReconfigurationState::ForcedUpdate
            || vendor.state == Biz::PresetUpdater::VendorReconfigurationState::ForcedDowngrade
            || vendor.state == Biz::PresetUpdater::VendorReconfigurationState::RemoveVendor)
        {
            return true;
        }
    }
    return false;
}

} // namespace

PresetUpdaterDialog::PresetUpdaterDialog(
    PresetUpdater::PresetUpdaterController& controller, Navigator& navigator
) :
    // TRN Preset updater dialog title.
    Dialog({Biz::_u8L("Preset sources & updates")}, "PresetUpdaterDialog"),
    m_controller(controller),
    m_navigator(navigator),
    m_online_filter(std::make_shared<SourceSortFilter>()),
    m_local_filter(std::make_shared<SourceSortFilter>())
{
    content_item()->set_modal(true);

    content_item()->set_width(100_ww);
    content_item()->set_max_width(k_preferred_max_width);
    content_item()->set_max_height(k_preferred_max_height);

    // Forced mode takes the close button away, and without it the header shrinks to its padding.
    if (Item* buttons_row = close_button()->parent_item()) {
        if (Item* top_row = buttons_row->parent_item()) {
            top_row->set_height(k_top_row_height);
        }
    }

    build_content();

    m_controller.add_listener<PresetUpdater::IPresetUpdaterControllerListener>(this);

    callbacks().opened = [this]() { m_controller.on_dialog_opened(); };
    callbacks().closed = [this]() { m_controller.on_dialog_closed(); };
}

PresetUpdaterDialog::~PresetUpdaterDialog()
{
    m_controller.remove_listener<PresetUpdater::IPresetUpdaterControllerListener>(this);
}

void PresetUpdaterDialog::build_content()
{
    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(12_fpx);

    m_forced_banner = content()->emplace_back<Item>();
    m_forced_banner->set_orientation(Orientation::Vertical);
    m_forced_banner->set_gap(4_fpx);
    m_forced_banner->set_width_percent(100);
    m_forced_banner->set_flex_shrink(0);
    m_forced_banner->set_visible(false);

    // TRN Preset updater dialog. Heading of the required updates banner.
    const std::string forced_heading = Biz::_u8L("Required configuration updates");
    Text* forced_heading_text =
        m_forced_banner->emplace_back<Text>(forced_heading, Render::ImguiFontType::Bold);
    forced_heading_text->set_text_color(m_theme->color_imgui(Platform::Color::Warning));

    // TRN Preset updater dialog. Body of the required updates banner.
    const std::string forced_body = Biz::_u8L(
        "The printer presets installed on this computer cannot be used until they are changed. "
        "Only the sources they came from are listed below. Apply the changes to continue, switch a "
        "source off to remove the presets it installed instead, or quit the application."
    );
    Text* forced_body_text = m_forced_banner->emplace_back<Text>(forced_body);
    forced_body_text->set_wrap_mode(Text::WrapMode::Wrap);

    ScrollArea* scroll_area = content()->emplace_back<ScrollArea>();
    scroll_area->set_orientation(Orientation::Vertical);
    scroll_area->set_flex_shrink(1);

    Item* sections = scroll_area->emplace_back<Item>();
    sections->set_orientation(Orientation::Vertical);
    sections->set_gap(16_fpx);
    sections->set_width_percent(100);
    sections->set_flex_shrink(0);

    // TRN Preset updater dialog. Heading above the sources served over the internet.
    sections->emplace_back<Text>(Biz::_u8L("Online sources"), Render::ImguiFontType::Bold);

    // TRN Preset updater dialog. Line under the "Online sources" heading.
    const std::string online_text = Biz::_u8L("Select the sources you want to install and update printer presets from.");
    Text* online_blurb            = sections->emplace_back<Text>(online_text);
    online_blurb->set_wrap_mode(Text::WrapMode::Wrap);

    // TRN Preset updater dialog. Shown under the "Online sources" heading.
    const std::string offline_text = Biz::_u8L(
        "Checking these sources over the internet is turned off in Preferences. Presets are still "
        "installed from the files that come with the application and from local sources."
    );
    m_offline_note = sections->emplace_back<Text>(offline_text);
    m_offline_note->set_wrap_mode(Text::WrapMode::Wrap);
    m_offline_note->set_text_color(m_theme->color_imgui(Platform::Color::Warning));

    m_online_list_view = sections->emplace_back<SourceListView>(SourceFactory{m_controller});
    m_online_list_view->set_orientation(Orientation::Vertical);
    m_online_list_view->set_gap(8_fpx);
    m_online_list_view->set_flex_shrink(0);
    m_online_filter->set_filter_fn([this](const PresetUpdater::SourceRowState& source)
                                   { return source_visible(source); });
    m_online_filter->set_source_model(&m_controller.online_sources());
    m_online_list_view->set_source_list(m_online_filter.get());

    Item* local_header = sections->emplace_back<Item>();
    local_header->set_orientation(Orientation::Horizontal);
    local_header->set_align_items(YGAlignCenter);
    local_header->set_gap(8_fpx);
    local_header->set_padding(
        Paddings{0_fpx, 0_fpx, PresetUpdaterRowLayout::source_frame_padding_horizontal, 0_fpx}
    );

    // TRN Preset updater dialog. Heading above the sources added from a ZIP archive.
    local_header->emplace_back<Text>(Biz::_u8L("Local sources"), Render::ImguiFontType::Bold);

    Item* local_header_spacer = local_header->emplace_back<Item>();
    local_header_spacer->set_flex_grow(1);

    // TRN Preset updater dialog button. Picks a ZIP archive to add as a source.
    const std::string add_zip_label = Biz::_u8L("Add ZIP");
    m_add_zip_button                = local_header->emplace_back<LayoutButton>(add_zip_label);

    m_add_zip_button->set_width(PresetUpdaterRowLayout::action_slot_width);
    PresetUpdaterRowLayout::apply_button_size(m_add_zip_button);
    m_add_zip_button->set_flex_shrink(0);
    m_add_zip_button->callbacks().action = [this]() { pick_zip_archive(); };

    // TRN Preset updater dialog. {} is a clickable web address.
    const std::string local_text = Biz::_u8L("Presets can also be updated from a ZIP file downloaded from {}.");

    Item* local_blurb = sections->emplace_back<Item>();
    local_blurb->set_flex_wrap(YGWrapWrap);
    local_blurb->set_align_items(YGAlignCenter);
    local_blurb->set_flex_shrink(0);

    const size_t placeholder = local_text.find("{}");
    append_words(local_blurb, std::string_view{local_text}.substr(0, placeholder));

    m_repo_link_button = local_blurb->emplace_back<LayoutButton>(k_preset_repo_url);
    m_repo_link_button->set_background_color(Platform::Color::ButtonTransparent);
    m_repo_link_button->set_label_color(m_theme->color_imgui(Platform::Color::TextLink));
    m_repo_link_button->set_content_padding({0_fpx, 0_fpx});
    m_repo_link_button->set_cursor(ImGuiMouseCursor_Hand);
    m_repo_link_button->callbacks().action = []()
    { open_browser(OpenBrowserParams{.url = k_preset_repo_url}); };

    if (placeholder != std::string::npos) {
        append_words(local_blurb, std::string_view{local_text}.substr(placeholder + 2));
    }

    m_local_list_view = sections->emplace_back<SourceListView>(SourceFactory{m_controller});
    m_local_list_view->set_orientation(Orientation::Vertical);
    m_local_list_view->set_gap(8_fpx);
    m_local_list_view->set_flex_shrink(0);
    m_local_filter->set_filter_fn([this](const PresetUpdater::SourceRowState& source)
                                  { return source_visible(source); });
    m_local_filter->set_source_model(&m_controller.local_sources());
    m_local_list_view->set_source_list(m_local_filter.get());

    Separator* footer_separator = add_separator();
    footer_separator->set_flex_shrink(0);
    build_footer(content());

    on_preset_updater_changed();
}

void PresetUpdaterDialog::build_footer(Item* parent)
{
    Item* footer = parent->emplace_back<Item>();
    footer->set_orientation(Orientation::Horizontal);
    footer->set_align_items(YGAlignCenter);
    footer->set_gap(8_fpx);
    footer->set_flex_shrink(0);

    // TRN Preset updater dialog button. Opens the application configuration folder.
    const std::string folder_label = Biz::_u8L("Configuration folder");
    LayoutButton* folder_button    = footer->emplace_back<LayoutButton>(folder_label);
    folder_button->set_min_width(PresetUpdaterRowLayout::action_slot_width);
    PresetUpdaterRowLayout::apply_button_size(folder_button);
    folder_button->set_flex_shrink(0);
    folder_button->callbacks().action = []()
    { AppServices::instance().file_explorer_handler().open_datadir_folder(); };

    Item* spacer = footer->emplace_back<Item>();
    spacer->set_flex_grow(1);

    m_update_everything_button = footer->emplace_back<LayoutButton>(apply_everything_label());
    m_update_everything_button->set_min_width(PresetUpdaterRowLayout::action_slot_width);
    PresetUpdaterRowLayout::apply_button_size(m_update_everything_button);
    m_update_everything_button->set_flex_shrink(0);
    m_update_everything_button->callbacks().action = [this]()
    {
        if (m_forced_mode) {
            m_controller.update_required();
        } else {
            m_controller.update_everything();
        }
        close_action();
    };

    // TRN Preset updater dialog button. Closes the dialog.
    m_footer_close_button = footer->emplace_back<LayoutButton>(Biz::_u8L("Close"));
    m_footer_close_button->set_min_width(PresetUpdaterRowLayout::action_slot_width);
    PresetUpdaterRowLayout::apply_button_size(m_footer_close_button);
    m_footer_close_button->set_flex_shrink(0);
    m_footer_close_button->callbacks().action = [this]() { close_action(); };

    // TRN Preset updater dialog button. Quits the application.
    m_exit_app_button = footer->emplace_back<LayoutButton>(Biz::_u8L("Quit"));
    m_exit_app_button->set_min_width(PresetUpdaterRowLayout::action_slot_width);
    PresetUpdaterRowLayout::apply_button_size(m_exit_app_button);
    m_exit_app_button->set_flex_shrink(0);
    m_exit_app_button->set_visible(false);
    m_exit_app_button->callbacks().action = [this]() { m_navigator.close_application(); };
}

bool PresetUpdaterDialog::source_visible(const PresetUpdater::SourceRowState& source) const
{
    if (!m_forced_mode) {
        return true;
    }
    if (source.required) {
        return true;
    }
    if (source.update_state == PresetUpdater::SourceRowState::UpdateState::Waiting
        || source.update_state == PresetUpdater::SourceRowState::UpdateState::Checking)
    {
        return true;
    }
    return has_forced_vendor(source);
}

void PresetUpdaterDialog::pick_zip_archive()
{
    // TRN Preset updater dialog. Title of the file picker for a source archive.
    const std::string title = Biz::_u8L("Select a ZIP archive with printer presets");
    AppServices::instance().dialog_manager().show_file_dialog(
        FileDialogType::Open,
        title,
        {},
        {},
        Wildcards::generate_wildcards(Wildcards::TypeFlag::Zip),
        [this](bool result, const std::vector<boost::filesystem::path>& file_paths)
        {
            if (!result || file_paths.empty()) {
                return;
            }
            m_controller.add_local_repository(file_paths.front());
        }
    );
}

void PresetUpdaterDialog::on_preset_updater_changed()
{
    m_offline_note->set_visible(!m_controller.online_allowed());

    const bool forced{m_controller.forced_mode()};
    const bool forced_state_cleared{m_forced_mode && !forced};
    if (m_forced_mode != forced) {
        m_forced_mode = forced;
        m_online_filter->invalidate();
        m_local_filter->invalidate();
    }

    m_add_zip_button->set_enabled(!forced);
    m_repo_link_button->set_enabled(!forced);

    // In forced mode the button installs the required vendors only, so anything else being
    // actionable must not make it look like there is something left to press.
    const bool actionable{forced ? m_controller.has_required_updates() :
                                   m_controller.has_actionable_updates()};
    const bool busy{m_controller.busy()};

    // Nothing running, every selected source answered, nothing gone wrong: only then is "up to
    // date" a statement about the sources rather than about how far the check has got.
    const bool settled{!busy && m_controller.fully_checked() && !m_controller.warned()};

    m_update_everything_button->set_enabled(actionable && !busy);
    if (forced) {
        m_update_everything_button->set_label(apply_required_label());
    } else if (!actionable && settled) {
        m_update_everything_button->set_label(up_to_date_label());
    } else {
        m_update_everything_button->set_label(apply_everything_label());
    }

    set_closable(!forced);
    m_footer_close_button->set_visible(!forced);
    m_exit_app_button->set_visible(forced);
    m_forced_banner->set_visible(forced);

    if (forced_state_cleared) {
        close();
    }
}

void PresetUpdaterDialog::resize(const SizeInfo& size_info)
{
    apply_size_limits(size_info);
    Dialog::resize(size_info);
}

void PresetUpdaterDialog::apply_size_limits(const SizeInfo& size_info)
{
    EvaluatedUnit preferred_width{k_preferred_max_width};
    preferred_width.evaluate(size_info);
    EvaluatedUnit preferred_height{k_preferred_max_height};
    preferred_height.evaluate(size_info);

    const float max_width = std::min(
        preferred_width.result,
        static_cast<float>(size_info.viewport_size_x) * k_viewport_fraction
    );
    const float max_height = std::min(
        preferred_height.result,
        static_cast<float>(size_info.viewport_size_y) * k_viewport_fraction
    );

    if (max_width != m_applied_max_width) {
        m_applied_max_width = max_width;
        content_item()->set_max_width(max_width);
    }
    if (max_height != m_applied_max_height) {
        m_applied_max_height = max_height;
        content_item()->set_max_height(max_height);
    }
}

void PresetUpdaterDialog::close_action()
{
    if (m_controller.forced_mode()) {
        return;
    }
    m_navigator.set_modal_dialog(ModalDialog::None);
}

} // namespace Slic3r::App
