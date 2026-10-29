#include "Slic3r/App/PrintModeDialog.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/WarningPanel.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

#include <fmt/format.h>

#include <optional>
#include <set>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

std::string param_to_string(const Domain::PrintModeParamValue& value)
{
    return std::visit(
        Domain::overloaded{
            [](bool v) { return std::string{v ? "true" : "false"}; },
            [](const std::string& v) { return v; },
            [](const auto& v) { return fmt::format("{}", v); },
        },
        value);
}

struct OverrideRow
{
    const Domain::ConfigItemDef* def{nullptr};
    std::string value;
};

std::string override_to_string(const Domain::ConfigItem& item)
{
    return item.value().holds_alternative<bool>()
        ? (item.get<bool>() ? Biz::_u8L("On") : Biz::_u8L("Off"))
        : ConfigItemUtils::config_item_to_string(item);
}

// One value when every slot agrees, otherwise the slots in order with "-" where untouched.
std::string slot_overrides_to_string(
    const std::vector<Domain::MacroSlotOverridesFDM>& slot_boxes, const std::string& key)
{
    std::vector<std::string> values;
    std::set<std::string> distinct;
    bool every_slot{true};
    for (const Domain::MacroSlotOverridesFDM& slot_box : slot_boxes) {
        if (const std::optional<Domain::ConfigItem> item{slot_box.overrides.get(key)}) {
            values.push_back(override_to_string(*item));
            distinct.insert(values.back());
        } else {
            values.push_back("-");
            every_slot = false;
        }
    }
    if (every_slot && distinct.size() == 1) {
        return values.front();
    }
    std::string result;
    for (const std::string& value : values) {
        result += (result.empty() ? "" : " / ") + value;
    }
    return result;
}

/// Hidden items are internal, the user cannot relate them to anything in the settings.
std::vector<OverrideRow> visible_overrides(const Domain::MacroOverridesLayerFDM& layer)
{
    std::vector<OverrideRow> rows;
    for (const Domain::ConfigItem& item : layer.overrides.overrides.overridden_items()) {
        if (item.def().category != Domain::ConfigItemDef::Category::Hidden) {
            rows.push_back({&item.def(), override_to_string(item)});
        }
    }
    std::set<std::string> slot_keys;
    for (const Domain::MacroSlotOverridesFDM& slot_box : layer.slot_overrides) {
        for (const Domain::ConfigItem& item : slot_box.overrides.overridden_items()) {
            if (item.def().category != Domain::ConfigItemDef::Category::Hidden) {
                slot_keys.insert(item.name());
            }
        }
    }
    for (const std::string& key : slot_keys) {
        rows.push_back(
            {&layer.slot_overrides.front().overrides.find(key)->def(),
             slot_overrides_to_string(layer.slot_overrides, key)});
    }
    return rows;
}

class PrintModeCard : public Item
{
public:
    PrintModeCard(
        Biz::PrintModeInteractor& interactor,
        const std::string& id,
        const std::string& info,
        const std::optional<std::string>& error,
        const Domain::MacroOverridesLayerFDM& layer,
        const bool active,
        const bool params_editable,
        std::set<std::string>& expanded_ids
    ) :
        m_interactor(interactor),
        m_id(id),
        m_params(layer.params),
        m_params_editable(params_editable),
        m_expanded_ids(expanded_ids)
    {
        set_orientation(Orientation::Vertical);
        set_align_items(YGAlignStretch);
        set_flex_shrink(0);

        m_block = emplace_back<Rectangle>();
        m_block->set_orientation(Orientation::Vertical);
        m_block->set_align_items(YGAlignStretch);
        m_block->set_flex_shrink(0);
        m_block->set_fill(m_theme->color_imgui(Platform::Color::Transparent));
        m_block->set_border_width(2);
        m_block->set_rounding(4);
        m_block->set_padding(2_fpx);

        m_button = m_block->emplace_back<RectangleButton>();
        m_button->set_checkable(true);
        m_button->set_allow_overlap(true);
        m_button->set_content_orientation(Orientation::Vertical);
        m_button->set_content_align_items(YGAlignStretch);
        m_button->set_content_padding({16_fpx, 12_fpx});
        m_button->set_content_gap(8_fpx);
        m_button->set_rounding(4);
        m_button->set_checked(active);
        // The button follows the preset. The click asks for the change, the dialog reloads
        // once the preset confirms it, so the button's own toggle is undone here.
        m_button->callbacks().action = [this, active] {
            m_button->set_checked(active);
            m_interactor.set_active(m_id, !active);
        };
        m_block->set_border_color(m_theme->color_imgui(
            active ? Platform::Color::Button : Platform::Color::Transparent,
            Platform::ColorGroup::Active));

        build_header(layer.name, info);
        if (error) {
            build_error();
        }
        build_param_rows();
        build_panel(layer);
        // The cards are rebuilt on every change, so the expanded state outlives them in the dialog.
        set_expanded(m_expanded_ids.contains(m_id));
    }

private:
    void build_header(const std::string& name, const std::string& info)
    {
        const ImColor dim{
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)};

        auto top{m_button->emplace_back<Item>()};
        top->set_orientation(Orientation::Horizontal);
        top->set_align_items(YGAlignCenter);
        top->set_gap(12_fpx);

        auto texts{top->emplace_back<Item>()};
        texts->set_orientation(Orientation::Vertical);
        texts->set_gap(2_fpx);
        texts->set_flex_grow(1);
        auto title{texts->emplace_back<Text>(name, Render::ImguiFontType::Bold)};
        title->set_font_size(15_fpx);
        m_info = texts->emplace_back<Text>(info);
        m_info->set_text_color(dim);
        m_info->set_wrap_mode(Text::WrapMode::WrapElide);
        m_info->set_visible(!info.empty());

        m_cog = top->emplace_back<LayoutButton>(std::string{}, Render::Icon::Cog);
        m_cog->set_checkable(true);
        m_cog->set_width(24_fpx);
        m_cog->set_height(24_fpx);
        m_cog->set_background_color(Platform::Color::ButtonTransparent);
        m_cog->callbacks().action = [this] { set_expanded(!m_panel->is_self_visible()); };
    }

    void set_expanded(const bool expanded)
    {
        if (expanded) {
            m_expanded_ids.insert(m_id);
        } else {
            m_expanded_ids.erase(m_id);
        }
        m_cog->set_checked(expanded);
        m_panel->set_visible(expanded);
        m_button->set_draw_flags(
            expanded ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    }

    void build_error()
    {
        auto panel{m_button->emplace_back<WarningPanel>(Platform::Color::Error)};
        panel->set_rounding(4);
        panel->set_flex_shrink(0);
        panel->set_warning(Biz::_u8L("Cannot be applied."), "Maybe your parameters are invlaid?");
    }

    void build_param_rows()
    {
        for (const auto& [name, param] : m_params) {
            auto row{m_button->emplace_back<Item>()};
            row->set_orientation(Orientation::Horizontal);
            row->set_align_items(YGAlignCenter);
            row->set_gap(12_fpx);
            row->set_padding({28_fpx, 0_fpx, 0_fpx, 0_fpx});
            auto label{row->emplace_back<Text>(param.label)};
            label->set_flex_grow(1);
            label->set_wrap_mode(Text::WrapMode::WrapElide);
            // Orphans have no plugin to re-evaluate them with, so their values are only shown.
            if (m_params_editable && std::holds_alternative<int>(param.value)) {
                auto input{
                    row->emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>())};
                input->set_width(168_fpx);
                input->set_text(param_to_string(param.value));
                input->callbacks().text_edited = [this, name, input] {
                    m_params[name].value = dynamic_cast<IntValidator*>(input->validator())->value();
                    m_interactor.set_mode_params(m_id, m_params);
                };
            } else {
                auto text{row->emplace_back<Text>(
                    param_to_string(param.value), Render::ImguiFontType::Bold)};
                text->set_width(168_fpx);
            }
        }
    }

    void build_panel(const Domain::MacroOverridesLayerFDM& layer)
    {
        const ImColor dim{
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)};

        m_panel = m_block->emplace_back<Rectangle>();
        m_panel->set_visible(false);
        m_panel->set_orientation(Orientation::Vertical);
        m_panel->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        m_panel->set_rounding(4);
        m_panel->set_flags(ImDrawFlags_RoundCornersBottom);
        m_panel->set_padding({32_fpx, 12_fpx, 16_fpx, 16_fpx});
        m_panel->set_gap(4_fpx);
        m_panel->set_flex_shrink(0);
        m_panel->set_align_items(YGAlignStretch);

        auto header{m_panel->emplace_back<Item>()};
        header->set_orientation(Orientation::Horizontal);
        header->set_gap(12_fpx);
        auto title{header->emplace_back<Text>(Biz::_u8L("Overridden settings"))};
        title->set_flex_grow(1);
        title->set_text_color(dim);
        auto column{header->emplace_back<Text>(Biz::_u8L("Mode override"))};
        column->set_width(180_fpx);
        column->set_text_color(dim);

        m_table = m_panel->emplace_back<Item>();
        m_table->set_orientation(Orientation::Vertical);
        m_table->set_align_items(YGAlignStretch);
        m_table->set_gap(3_fpx);

        std::vector<OverrideRow> rows{visible_overrides(layer)};
        std::ranges::sort(rows, {}, [](const OverrideRow& row) {
            return std::tuple{row.def->category, row.def->option_group, row.def->order};
        });

        // The label alone is ambiguous (e.g. "Perimeters" exists in several groups), so each
        // run of items from the same category and group gets a dim heading in its own row.
        std::optional<std::pair<Domain::ConfigItemDef::Category, Domain::ConfigItemDef::OptionGroup>>
            last_group;
        for (const OverrideRow& override_row : rows) {
            const std::pair group{override_row.def->category, override_row.def->option_group};
            if (last_group != group) {
                last_group = group;
                auto heading{m_table->emplace_back<Text>(fmt::format(
                    "{} · {}",
                    Biz::_u8(Domain::ConfigItemDef::translate_category(
                        group.first, Domain::PrinterTechnology::FFF)),
                    Biz::_u8(Domain::ConfigItemDef::translate_option_group(group.second))))};
                heading->set_text_color(dim);
                heading->set_wrap_mode(Text::WrapMode::WrapElide);
                heading->set_padding({0_fpx, 8_fpx, 0_fpx, 2_fpx});
                heading->set_flex_shrink(0);
            }
            auto row{m_table->emplace_back<Item>()};
            row->set_orientation(Orientation::Horizontal);
            row->set_gap(12_fpx);
            row->set_padding({12_fpx, 5_fpx, 0_fpx, 5_fpx});
            row->set_flex_shrink(0);
            auto label{row->emplace_back<Text>(Biz::_u8(override_row.def->label))};
            label->set_flex_grow(1);
            label->set_flex_shrink(1);
            label->set_wrap_mode(Text::WrapMode::WrapElide);
            auto cell{row->emplace_back<Text>(override_row.value, Render::ImguiFontType::Bold)};
            cell->set_width(168_fpx);
        }
    }

    Biz::PrintModeInteractor& m_interactor;
    std::string m_id;
    Domain::PrintModeParams m_params;
    bool m_params_editable{true};
    std::set<std::string>& m_expanded_ids;

    Rectangle* m_block{nullptr};
    RectangleButton* m_button{nullptr};
    Text* m_info{nullptr};
    LayoutButton* m_cog{nullptr};
    Rectangle* m_panel{nullptr};
    Item* m_table{nullptr};
};


std::string mode_info(const Biz::PrintMode& mode)
{
    if (mode.error) {
        // The card shows the error itself instead.
        return {};
    }
    return fmt::format(
        fmt::runtime(Biz::_u8L("Changes {} settings · {} adjustable")),
        visible_overrides(mode.layer).size(),
        mode.layer.params.size());
}

} // namespace

PrintModeDialog::PrintModeDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Dialog({Biz::_u8L("Print mode")}, "PrintModeDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_print_mode_listener_scope(project_interactor.print_mode_interactor(), *this),
    m_preset_interactor_listener_scope(project_interactor.preset_interactor(), *this),
    m_selected_project_listener_scope(project_interactor, *this),
    m_selected_config_container_listener_scope(project_interactor, *this),
    m_config_container_list_listener_scope(project_interactor, *this),
    m_projects_listener_scope(project_interactor, *this)
{
    content_item()->set_width(620_fpx);
    content_item()->set_height(640_fpx);
    content()->set_align_items(YGAlignStretch);

    m_list = content()->emplace_back<ScrollArea>();
    m_list->set_orientation(Orientation::Vertical);
    m_list->set_align_items(YGAlignStretch);
    m_list->set_flex_grow(1);
    m_list->set_gap(12_fpx);
    m_list->set_padding({12_fpx, 4_fpx});

    reload();
}

void PrintModeDialog::on_print_modes_changed() {
    m_reload_pending = true;
}

void PrintModeDialog::on_macro_overrides_changed() {
    m_reload_pending = true;
}

void PrintModeDialog::on_selected_project_changed_final(size_t) {
    // The active flags follow the selected preset, which changes with the project.
    m_reload_pending = true;
}

void PrintModeDialog::on_selected_config_container_changed(Domain::SelectionId, Domain::SelectionId)
{
    // The expanded state is per config container, and the print modes may not report a change.
    m_reload_pending = true;
}

void PrintModeDialog::on_config_container_list_changed(Domain::SelectionId)
{
    prune_expanded_ids();
}

void PrintModeDialog::on_project_removed(Domain::SelectionId)
{
    prune_expanded_ids();
}

void PrintModeDialog::prune_expanded_ids()
{
    // The notifications do not say which container went away, so keep only the existing ones.
    std::set<Domain::SelectionId> existing;
    for (const auto& [project_id, project] : m_project_interactor.workbench().projects()) {
        for (const auto& container : project.config_containers()) {
            existing.insert(container->id().id);
        }
    }
    std::erase_if(m_expanded_ids, [&](const auto& entry) { return !existing.contains(entry.first); });
}

void PrintModeDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void PrintModeDialog::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    if (m_reload_pending) {
        m_reload_pending = false;
        reload();
    }
    Dialog::render(pos, size);
}

void PrintModeDialog::reload() {
    while (!m_list->items().empty()) {
        m_list->remove(m_list->items().back());
    }
    // A mode is active when its layer is applied in the selected preset.
    const Domain::MacroOverridesMapFDM* applied{nullptr};
    if (m_project_interactor.selected_project_id() != Domain::INVALID_ID) {
        applied = std::get_if<Domain::MacroOverridesMapFDM>(
            &m_project_interactor.preset_interactor().selected_printer_preset().macro_overrides);
    }
    const auto is_active{[&](const std::string& id) { return applied && applied->contains(id); }};

    std::set<std::string>& expanded_ids{
        m_expanded_ids[m_project_interactor.selected_config_container_id()]};
    Biz::PrintModeInteractor& interactor{m_project_interactor.print_mode_interactor()};
    for (const auto& [id, mode] : interactor.modes()) {
        m_list->emplace_back<PrintModeCard>(
            interactor,
            id,
            mode_info(mode),
            mode.error,
            mode.layer,
            is_active(id),
            true,
            expanded_ids);
    }
    for (const auto& [id, layer] : interactor.orphaned_modes()) {
        m_list->emplace_back<PrintModeCard>(
            interactor,
            id,
            Biz::_u8L("Loaded from the project, the plugin is not installed"),
            std::nullopt,
            layer,
            is_active(id),
            false,
            expanded_ids);
    }
}

} // namespace Slic3r::App
