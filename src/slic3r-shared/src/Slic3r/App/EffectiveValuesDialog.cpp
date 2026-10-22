#include "Slic3r/App/EffectiveValuesDialog.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/ExtruderIcon.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/EffectiveValues.hpp"
#include "Slic3r/Domain/MacroOverrides.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <set>

namespace Slic3r::App {

using Yoga::operator""_fpx;

namespace {

std::string to_display_string(bool value) { return value ? "true" : "false"; }

std::string to_display_string(const std::optional<int>& value)
{
    return value.has_value() ? std::to_string(*value) : "-";
}

std::string to_display_string(const Domain::Vec2d& value)
{
    return fmt::format("[{:.10g}, {:.10g}]", value[0], value[1]);
}

std::string
scalar_to_display_string(const Domain::ConfigItem& item, const Domain::ConfigValue& value)
{
    return value.visit(Domain::overloaded{
        [](const bool scalar) { return to_display_string(scalar); },
        [](const std::optional<int>& scalar) { return to_display_string(scalar); },
        [](const Domain::Vec2d& scalar) { return to_display_string(scalar); },
        [&](const auto&) { return ConfigItemUtils::config_item_to_string(item, value); },
    });
}

// One string per element for vectors, a single string for scalars.
std::vector<std::string>
to_display_strings(const Domain::ConfigItem& item, const Domain::ConfigValue& value)
{
    std::vector<std::string> result;
    value.visit(Domain::overloaded{
        [&](const Domain::EnumVectorWrapper& enum_vector)
        {
            const Domain::EnumValueDefs& defs{enum_vector.def()};
            for (const size_t index : enum_vector.get_indexes()) {
                result.push_back(defs.at(index).str_ui);
            }
        },
        [&]<typename T>(const std::vector<T>& values)
        {
            for (const T& element : values) {
                result.push_back(scalar_to_display_string(item, Domain::ConfigValue{element}));
            }
        },
        [&](const auto&) { result.push_back(scalar_to_display_string(item, value)); },
    });
    return result;
}

bool is_vector_value(const Domain::ConfigValue& value)
{
    return value.visit(Domain::overloaded{
        [](const Domain::EnumVectorWrapper&) { return true; },
        []<typename T>(const std::vector<T>&) { return true; },
        [](const auto&) { return false; },
    });
}

// Whether the value can differ per material slot (tool print, filament, parity, rules).
bool is_per_slot(const Domain::ConfigItemDef& def)
{
    if (def.require_tool_parity || def.compatibility_rule != Domain::CompatibilityRule::Undefined) {
        return true;
    }
    if (def.overrides_in.contains(Domain::FDMConfigLocation::Tool)) {
        return true;
    }
    if (const auto* fdm{std::get_if<Domain::FDMConfigLocation>(&def.location)}) {
        return *fdm == Domain::FDMConfigLocation::Tool
            || *fdm == Domain::FDMConfigLocation::Filament;
    }
    if (const auto* sla{std::get_if<Domain::SLAConfigLocation>(&def.location)}) {
        return *sla == Domain::SLAConfigLocation::Material;
    }
    return false;
}

bool is_tool_location(const Domain::ConfigLocation& location)
{
    const auto* fdm{std::get_if<Domain::FDMConfigLocation>(&location)};
    return fdm && *fdm == Domain::FDMConfigLocation::Tool;
}

bool is_print_location(const Domain::ConfigLocation& location)
{
    const auto* fdm{std::get_if<Domain::FDMConfigLocation>(&location)};
    return fdm && *fdm == Domain::FDMConfigLocation::Print;
}

bool is_macro_override(const Domain::ConfigLocation& location)
{
    if (const auto* fdm{std::get_if<Domain::FDMConfigLocation>(&location)}) {
        return *fdm == Domain::FDMConfigLocation::MacroOverrides;
    }
    if (const auto* sla{std::get_if<Domain::SLAConfigLocation>(&location)}) {
        return *sla == Domain::SLAConfigLocation::MacroOverrides;
    }
    return false;
}

std::string location_label(const Domain::ConfigLocation& location)
{
    return std::visit(
        Domain::overloaded{
            [](const Domain::FDMConfigLocation location) -> std::string
            {
                switch (location) {
                case Domain::FDMConfigLocation::Printer: return Biz::_u8L("Printer settings");
                case Domain::FDMConfigLocation::Tool: return Biz::_u8L("Print settings");
                case Domain::FDMConfigLocation::Print: return Biz::_u8L("Print settings");
                case Domain::FDMConfigLocation::Filament: return Biz::_u8L("Filament settings");
                case Domain::FDMConfigLocation::Project: return Biz::_u8L("Project settings");
                case Domain::FDMConfigLocation::Object: return Biz::_u8L("Object settings");
                case Domain::FDMConfigLocation::Volume: return Biz::_u8L("Part settings");
                case Domain::FDMConfigLocation::MacroOverrides: return Biz::_u8L("Mode override");
                case Domain::FDMConfigLocation::None: return {};
                }
                PANIC("Unknown location");
            },
            [](const Domain::SLAConfigLocation location) -> std::string
            {
                switch (location) {
                case Domain::SLAConfigLocation::Printer: return Biz::_u8L("Printer settings");
                case Domain::SLAConfigLocation::Material: return Biz::_u8L("Material settings");
                case Domain::SLAConfigLocation::Print: return Biz::_u8L("Print settings");
                case Domain::SLAConfigLocation::Object: return Biz::_u8L("Object settings");
                case Domain::SLAConfigLocation::MacroOverrides: return Biz::_u8L("Mode override");
                case Domain::SLAConfigLocation::None: return {};
                }
                PANIC("Unknown location");
            },
            [](const Domain::PhysicalPrinterLocation&) -> std::string
            { return Biz::_u8L("Physical printer"); },
            [](const Domain::AppConfigLocation&) -> std::string
            { return Biz::_u8L("Application settings"); },
        },
        location);
}

std::string def_label(const std::string& key, Domain::PrinterTechnology technology)
{
    const Domain::ConfigItemDef* def{Domain::find_def(key, technology)};
    return def ? Biz::_u8(def->label) : key;
}

std::string step_label(
    const Domain::EffectiveValue::Modification& modification,
    Domain::PrinterTechnology technology,
    const std::string& macro_label)
{
    using namespace Domain;
    return std::visit(
        overloaded{
            [&](const EffectiveValue::SetBy& set_by) { return location_label(set_by.location); },
            [&](const EffectiveValue::OverriddenBy& overridden_by)
            {
                if (is_macro_override(overridden_by.location)) {
                    return fmt::format(fmt::runtime(Biz::_u8L("Overridden by {}")), macro_label);
                }
                if (is_tool_location(overridden_by.location)) {
                    // Per-tool print settings are presented as plain print settings.
                    return location_label(overridden_by.location);
                }
                return fmt::format(
                    fmt::runtime(Biz::_u8L("Overridden in {}")),
                    location_label(overridden_by.location));
            },
            [&](const EffectiveValue::CompatibilityRuleApplied& rule_applied)
            {
                if (!rule_applied.used_rule) {
                    return Biz::_u8L(
                        "The tools used by this print share the same value, so it is used as is");
                }
                switch (rule_applied.rule) {
                case CompatibilityRule::IgnoreOverrides:
                    return Biz::_u8L(
                        "A single value is required, a single fallback value is taken");
                case CompatibilityRule::Min:
                    return Biz::_u8L(
                        "A single value is required, the minimum over the used tools is taken");
                case CompatibilityRule::Max:
                    return Biz::_u8L(
                        "A single value is required, the maximum over the used tools is taken");
                case CompatibilityRule::Average:
                    return Biz::_u8L(
                        "A single value is required, the average over the used tools is taken");
                case CompatibilityRule::Undefined:
                    break;
                }
                return Biz::_u8L("A single value is required");
            },
            [&](const EffectiveValue::SlotParityEnsured&)
            { return Biz::_u8L("Applied to all tools"); },
            [&](const EffectiveValue::ExpandedFrom& expanded_from)
            {
                return fmt::format(
                    fmt::runtime(Biz::_u8L("Taken from {}")),
                    def_label(expanded_from.key, technology));
            },
            [&](const EffectiveValue::ResolvedFloatOrPercentage& resolved)
            {
                return fmt::format(
                    fmt::runtime(Biz::_u8L("Percentage resolved against {}")),
                    def_label(resolved.ratio_over, technology));
            },
            [&](const EffectiveValue::ClampedToSingleSlot&)
            {
                return Biz::_u8L(
                    "Extruder does not make sense in this context, the first extruder is used");
            },
        },
        modification.payload);
}

} // namespace

Yoga::Unit fpx(float value) { return Yoga::Unit{value, Yoga::Unit::Type::FigmaPixel}; }

// A grid with a label column and either one column per tool or a single value column.
class ValueTable : public Yoga::Item
{
public:
    struct Tools
    {
        std::vector<ImColor> colors;
        std::set<size_t> used; // Empty means all tools are used.
    };

    struct Row
    {
        enum class Kind
        {
            Effective, // Highlighted result row.
            Section,   // Bold text spanning the whole row.
            Step       // One step of the history.
        };

        Kind kind{Kind::Step};
        std::string label;
        std::vector<std::string> values;
        bool per_tool{false}; // values has one entry per tool.
    };

    ValueTable()
    {
        set_orientation(Yoga::Orientation::Vertical);
        set_align_items(YGAlignFlexStart);
        set_flex_shrink(0);
    }

    void clear()
    {
        while (!items().empty()) {
            remove(items().back());
        }
        m_column_count = 1;
    }

    void set_rows(const Tools& tools, const std::vector<Row>& rows)
    {
        clear();
        m_column_count = get_column_count(tools, rows);
        if (m_column_count > 1) {
            add_header(tools);
        }
        for (const Row& row : rows) {
            add_row(row);
        }
    }

    Yoga::Unit width() const
    {
        return fpx(LabelWidth + ColumnWidth * static_cast<float>(m_column_count));
    }

private:
    // All in Figma pixels.
    static constexpr float LabelWidth   = 210.f;
    static constexpr float ColumnWidth  = 72.f;
    static constexpr float RowHeight    = 28.f;
    static constexpr float LabelPadding = 10.f;

    static size_t get_column_count(const Tools& tools, const std::vector<Row>& rows)
    {
        return std::ranges::any_of(rows, &Row::per_tool) ? tools.colors.size() : 1;
    }

    void add_header(const Tools& tools)
    {
        auto row{add_row_item(false)};
        auto spacer{row->emplace_back<Yoga::Item>()};
        spacer->set_width(fpx(LabelWidth));
        for (size_t tool_index{}; tool_index < tools.colors.size(); ++tool_index) {
            auto cell{row->emplace_back<Yoga::Item>()};
            cell->set_width(fpx(ColumnWidth));
            cell->set_align_items(YGAlignCenter);
            cell->set_justify_content(YGJustifyCenter);
            auto icon{cell->emplace_back<ExtruderIcon>()};
            icon->set_width(22_fpx);
            icon->set_height(22_fpx);
            icon->set_extruder_index(tool_index + 1);
            icon->set_extruder_color(tools.colors[tool_index]);
            icon->set_enabled(tools.used.empty() || tools.used.contains(tool_index));
        }
    }

    void add_row(const Row& row)
    {
        const bool highlighted{row.kind == Row::Kind::Effective};
        auto row_item{add_row_item(highlighted)};

        if (row.kind == Row::Kind::Section) {
            auto label{row_item->emplace_back<Yoga::Text>(row.label, Render::ImguiFontType::Bold)};
            label->set_flex_grow(1);
            label->set_align({Yoga::AlignH::Left, Yoga::AlignV::Center});
            label->set_margin({fpx(LabelPadding), 12_fpx, 0_fpx, 0_fpx});
            return;
        }

        const ImColor text_color{m_theme->color_imgui(
            Platform::Color::Text,
            highlighted ? Platform::ColorGroup::Default : Platform::ColorGroup::Disabled)};
        const Render::ImguiFontType font{
            highlighted ? Render::ImguiFontType::Bold : Render::ImguiFontType::Regular};

        auto label_cell{row_item->emplace_back<Yoga::Item>()};
        label_cell->set_orientation(Yoga::Orientation::Vertical);
        label_cell->set_width(fpx(LabelWidth));
        label_cell->set_padding({fpx(LabelPadding), 4_fpx});
        label_cell->set_justify_content(YGJustifyCenter);
        auto label{label_cell->emplace_back<Yoga::Text>(row.label, font)};
        label->set_width_percent(100);
        label->set_wrap_mode(Yoga::Text::WrapMode::Wrap);
        label->set_align({Yoga::AlignH::Left, Yoga::AlignV::Center});
        label->set_text_color(text_color);

        if (row.per_tool) {
            for (const std::string& value : row.values) {
                auto cell{row_item->emplace_back<Yoga::Text>(value, font)};
                cell->set_width(fpx(ColumnWidth));
                cell->set_align({Yoga::AlignH::Center, Yoga::AlignV::Center});
                cell->set_text_color(text_color);
            }
        } else {
            // A single value spans all columns: centered over tool columns,
            // left aligned next to the label otherwise.
            auto cell{row_item->emplace_back<Yoga::Text>(join(row.values), font)};
            cell->set_width(fpx(ColumnWidth * static_cast<float>(m_column_count)));
            cell->set_padding({fpx(LabelPadding), 0_fpx});
            cell->set_align(
                {m_column_count > 1 ? Yoga::AlignH::Center : Yoga::AlignH::Left,
                 Yoga::AlignV::Center});
            cell->set_text_color(text_color);
        }
    }

    Yoga::Rectangle* add_row_item(bool highlighted)
    {
        auto row{emplace_back<Yoga::Rectangle>()};
        row->set_orientation(Yoga::Orientation::Horizontal);
        row->set_align_items(YGAlignStretch);
        row->set_min_height(fpx(RowHeight));
        row->set_width(width());
        row->set_rounding(4);
        row->set_flex_shrink(0);
        row->set_fill(
            highlighted
                ? m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)
                : m_theme->color_imgui(Platform::Color::Transparent));
        return row;
    }

    static std::string join(const std::vector<std::string>& values)
    {
        std::string result;
        for (const std::string& value : values) {
            if (!result.empty()) {
                result += ", ";
            }
            result += value;
        }
        return result;
    }

    size_t m_column_count{1};
};

EffectiveValuesDialog::EffectiveValuesDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Yoga::Dialog("EffectiveValuesDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    set_closable(true);
    content_item()->set_modal(true);
    content_item()->set_width(380_fpx);
    content_item()->set_height(200_fpx);

    while(!content_item()->items().empty()) {
        content_item()->remove(content_item()->items().back());
    }

    auto area{content_item()->emplace_back<Yoga::Rectangle>()};
    area->set_orientation(Yoga::Orientation::Vertical);
    area->set_fill(m_theme->color_imgui(Platform::Color::WindowBg));
    area->set_border_color(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    area->set_flex_grow(1);
    area->set_border_width(1);
    area->set_rounding(0);
    area->set_align_items(YGAlignStretch);

    const auto title_row_height{40_fpx};

    auto title_row{area->emplace_back<Yoga::Rectangle>()};
    title_row->set_height(title_row_height);
    title_row->set_align_items(YGAlignStretch);
    title_row->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));

    auto spacer{title_row->emplace_back<Yoga::Item>()};
    spacer->set_width(title_row_height);

    m_title = title_row->emplace_back<Yoga::Text>("");
    m_title->set_flex_grow(1);
    m_title->set_align({Yoga::AlignH::Center, Yoga::AlignV::Center});
    m_title->set_font_size(15_fpx);
    m_title->set_font_type(Render::ImguiFontType::Bold);

    auto close_button_area{title_row->emplace_back<Yoga::Item>()};
    close_button_area->set_width(title_row_height);
    close_button_area->set_align_items(YGAlignCenter);
    close_button_area->set_justify_content(YGJustifyCenter);

    auto close_button{
        close_button_area->emplace_back<Yoga::LayoutButton>(std::string{}, Render::Icon::PrintIdle)};
    close_button->set_width(20_fpx);
    close_button->set_height(20_fpx);
    close_button->callbacks().action = [this] { close_action(); };

    auto content{area->emplace_back<Yoga::ScrollArea>()};
    content->set_orientation(Yoga::Orientation::Vertical);
    content->set_align_items(YGAlignStretch);
    content->set_flex_grow(1);
    content->set_padding(20_fpx);
    content->set_gap(20_fpx);

    m_description = content->emplace_back<Yoga::Text>("");
    m_description->set_wrap_mode(Yoga::Text::WrapMode::Wrap);
    m_description->set_flex_shrink(0);

    m_table = content->emplace_back<ValueTable>();
    m_table->set_self_align(YGAlignCenter);
}

std::vector<ImColor> EffectiveValuesDialog::slot_colors(size_t tool_count) const
{
    const std::vector<Domain::ColorRGB> colors{
        m_project_interactor.project_settings_interactor().get_colors(
            m_project_interactor.selected_project_id(),
            m_project_interactor.selected_config_container_id())};

    std::vector<ImColor> result;
    result.reserve(tool_count);
    for (size_t index{}; index < tool_count; ++index) {
        Domain::ColorRGB color;
        if (index < colors.size()) {
            color = colors[index];
        } else {
            ASSERT(Biz::Algorithms::Color::decode_color(
                Biz::ProjectSettingsInteractor::palette_color(static_cast<int>(index)),
                color));
        }
        result.push_back(ImColor{color.r(), color.g(), color.b()});
    }
    return result;
}

std::string EffectiveValuesDialog::macro_override_label(const std::string& key) const
{
    const Domain::MacroOverridesMap& macro_overrides{
        m_project_interactor.selected_config_container().selected_preset().macro_overrides};

    if (const auto* fdm{std::get_if<Domain::MacroOverridesMapFDM>(&macro_overrides)}) {
        for (const auto& [id, layer] : *fdm) {
            if (!layer.overrides.overrides.get(key).has_value() || layer.name.empty()) {
                continue;
            }
            return layer.name;
        }
    }
    return Biz::_u8L("mode override");
}

void EffectiveValuesDialog::close_action()
{
    m_navigator.set_modal_dialog(ModalDialog::None);
}

void EffectiveValuesDialog::set_key(const std::optional<std::string>& key)
{
    clear();

    m_key = key;
    if (!m_key) {
        return;
    }
    const Domain::ConfigContainer& config_container{
        m_project_interactor.selected_config_container()};
    const Domain::Preset::SelectedPreset& selected_preset{config_container.selected_preset()};
    const Domain::PrinterTechnology printer_technology{selected_preset.technology()};

    const Domain::ConfigItemDef* def{Domain::find_def(*key, printer_technology)};
    if (!def) {
        return;
    }

    m_title->set_text(Biz::_u8(def->label));
    m_description->set_text(Biz::_u8(def->tooltip));
    update_effective_value_table(*key);
}

void EffectiveValuesDialog::update_effective_value_table(const std::string& key) {
    const Biz::Slicing::SlicingInteractor& slicing_interactor{
        m_project_interactor.slicing_interactor()};
    const Biz::Slicing::BackgroundProcessState* state{
        slicing_interactor.find_process_state(m_project_interactor.selected_bed_slicing_id())};

    m_table->clear();

    if (!state) {
        return;
    }

    auto it{state->effective_values.print.find(key)};
    if (it == state->effective_values.print.end()) {
        return;
    }

    const std::vector<Domain::EffectiveValue::Modification>& modifications{
        it->second.modifications};

    if (modifications.empty()) {
        return;
    }


    const Domain::ConfigContainer& config_container{
        m_project_interactor.selected_config_container()};
    const Domain::Preset::SelectedPreset& selected_preset{config_container.selected_preset()};
    const Domain::PrinterTechnology printer_technology{selected_preset.technology()};
    const Domain::ConfigItemDef* def{Domain::find_def(key, printer_technology)};
    if (!def) {
        return;
    }

    const size_t tool_count{selected_preset.hw_config.tool_count};
    const size_t slot_count{selected_preset.hw_config.material_slot_count()};
    const ValueTable::Tools tools{
        slot_colors(slot_count),
        {state->extruder_candidates.begin(), state->extruder_candidates.end()}};
    // Columns are material slots; the print/tool merge below depends on the tool count.
    const bool per_slot_item{slot_count > 1 && is_per_slot(*def)};
    const bool multitool{tool_count > 1};
    const std::string macro_label{macro_override_label(key)};


    const Domain::ConfigItem item{*def, def->location};

    const auto make_row{[&](ValueTable::Row::Kind kind,
                            const std::string& label,
                            const Domain::ConfigValue& value)
    {
        std::vector<std::string> values{to_display_strings(item, value)};
        bool per_tool{false};
        if (per_slot_item && !is_vector_value(value)) {
            values.resize(slot_count, values.front());
            per_tool = true;
        } else if (per_slot_item && values.size() == slot_count) {
            per_tool = true;
        }
        return ValueTable::Row{kind, label, std::move(values), per_tool};
    }};

    const auto is_set_in_print{[](const Domain::EffectiveValue::Modification& modification)
    {
        const auto* set_by{std::get_if<Domain::EffectiveValue::SetBy>(&modification.payload)};
        return set_by && is_print_location(set_by->location);
    }};
    const auto is_overridden_by_tool{[](const Domain::EffectiveValue::Modification& modification)
    {
        const auto* overridden_by{
            std::get_if<Domain::EffectiveValue::OverriddenBy>(&modification.payload)};
        return overridden_by && is_tool_location(overridden_by->location);
    }};
    const auto is_unused_rule{[](const Domain::EffectiveValue::Modification& modification)
    {
        const auto* rule{
            std::get_if<Domain::EffectiveValue::CompatibilityRuleApplied>(&modification.payload)};
        return rule && !rule->used_rule;
    }};

    // Print and tool print settings are presented as one "Print settings" step:
    // on a multitool printer the per-tool override replaces the print step,
    // on a single tool printer the tool override (if any) is redundant with it.
    std::vector<Domain::EffectiveValue::Modification> to_display;
    for (size_t index{}; index < modifications.size(); ++index) {
        const Domain::EffectiveValue::Modification& modification{modifications[index]};
        const bool replaced_by_tool{
            multitool && is_set_in_print(modification) && index + 1 < modifications.size()
            && is_overridden_by_tool(modifications[index + 1])};
        const bool redundant_tool{!multitool && is_overridden_by_tool(modification)};
        if (replaced_by_tool || redundant_tool || is_unused_rule(modification)) {
            continue;
        }
        to_display.push_back(modification);
    }

    // A single step has nothing to explain.
    if (to_display.size() <= 1) {
        return;
    }

    std::vector<ValueTable::Row> rows;
    const Domain::ConfigValue& effective{modifications.back().value};
    rows.push_back(
        make_row(ValueTable::Row::Kind::Effective, Biz::_u8L("Effective value"), effective));
    rows.push_back({ValueTable::Row::Kind::Section, Biz::_u8L("How this value was determined")});
    for (const Domain::EffectiveValue::Modification& modification : to_display) {
        rows.push_back(make_row(
            ValueTable::Row::Kind::Step,
            step_label(modification, printer_technology, macro_label),
            modification.value));
    }

    m_table->set_rows(tools, rows);
    // Grow the dialog so that all tool columns fit next to the content padding.
    content_item()->set_width(fpx(std::max(380.0f, m_table->width().value + 60.f)));
    content_item()->set_height(400_fpx);
}

void EffectiveValuesDialog::clear()
{
    m_title->set_text(Biz::_u8L("Settings"));
    m_description->set_text("");
    m_table->clear();
    content_item()->set_width(380_fpx);
    content_item()->set_height(200_fpx);
}

} // namespace Slic3r::App
