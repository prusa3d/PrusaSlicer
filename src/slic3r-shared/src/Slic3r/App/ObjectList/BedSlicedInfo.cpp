#include "Slic3r/App/ObjectList/BedSlicedInfo.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include "Slic3r/Domain/PrintStatistics.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"

#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"

#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Time.hpp"

#include <algorithm>
#include <optional>

using namespace Slic3r::Biz;

namespace Slic3r::App::ObjectList {

using namespace Yoga;

namespace {

// Column widths shared by all rows, so the values are aligned.
Unit label_column_width()
{
    return 100_fpx;
}

Unit value_column_width()
{
    return 60_fpx;
}

/**
 * @brief Adds an empty row to the parent. The name identifies the row in the UI tests.
 */
Item* add_row_item(Item* parent, const std::string& name)
{
    Item* row = parent->emplace_back<Item>();
    row->set_object_name(name);
    row->set_gap(10_fpx);
    row->set_align_items(YGAlignCenter);
    // The parent scroll area scrolls instead of squashing the rows.
    row->set_flex_shrink(0.f);
    return row;
}

Text* add_value_cell(Item* row, const std::string& text = {})
{
    Text* value = row->emplace_back<Text>(text);
    value->set_width(value_column_width());
    return value;
}

Text* add_last_cell(Item* row)
{
    Text* value = row->emplace_back<Text>(std::string{});
    value->set_flex_grow(1);
    value->set_wrap_mode(Text::WrapMode::WrapElide);
    return value;
}

/**
 * @brief Adds the "with spool" label, explaining the calculation in its tooltip.
 */
AbstractButton* add_with_spool_label(
    Item* parent,
    const std::string& label,
    const std::string& object_name,
    const ImColor& text_color
)
{
    AbstractButton* button = parent->emplace_back<AbstractButton>(
        _u8L("Used material plus the spool weight set in the filament profile assigned to the "
             "slot."),
        object_name
    );
    button->set_cursor(ImGuiMouseCursor_Arrow);
    Text* text = button->emplace_back<Text>(label);
    text->set_text_color(text_color);
    text->set_flex_grow(1);
    text->set_wrap_mode(Text::WrapMode::WrapElide);
    return button;
}

std::string format_weight(const float weight_g)
{
    return format("%1$.2f g", weight_g);
}

std::string format_length(const float length_mm)
{
    return format("%1$.2f m", length_mm / 1000.f);
}

/**
 * @return The spool weight of the profile assigned to the slot of the tool, nothing if the
 * profile has no spool weight or the tool is not a physical slot (e.g. a virtual extruder).
 */
std::optional<float> find_spool_weight(const std::vector<float>& spool_weights, const size_t tool)
{
    if (tool < spool_weights.size() && spool_weights[tool] > 0.f) {
        return spool_weights[tool];
    }
    return std::nullopt;
}

} // namespace

class BedSlicedInfo::ExtruderRowView : public Item, public DataObserver<ExtruderRowData>
{
public:
    ExtruderRowView(size_t index, const ExtruderRowData& data) :
        DataObserver<ExtruderRowData>(index, data)
    {
        set_object_name("BedEstimatesExtruderRow");
        set_gap(10_fpx);
        set_align_items(YGAlignCenter);

        Item* icon_cell = emplace_back<Item>();
        icon_cell->set_width(label_column_width());
        icon_cell->set_align_items(YGAlignCenter);
        icon_cell->emplace_back<Item>()->set_width(10_fpx);
        m_icon = icon_cell->emplace_back<ExtruderIcon>();
        m_icon->set_width(20_fpx);
        m_icon->set_height(20_fpx);

        m_used       = add_value_cell(this);
        m_length     = add_value_cell(this);
        m_with_spool = add_last_cell(this);
        m_with_spool->set_text_color(
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
        );
        // The values are set by ListView, it updates the inserted views.
    }

protected:
    void on_data_update() override
    {
        m_icon->set_extruder_color(m_state->color);
        m_icon->set_extruder_index(m_state->extruder_index);
        m_used->set_text(m_state->used);
        m_length->set_text(m_state->length);
        m_with_spool->set_text(m_state->with_spool);
        m_with_spool->set_visible(m_state->with_spool_visible);
    }

private:
    ExtruderIcon* m_icon{nullptr};
    Text* m_used{nullptr};
    Text* m_length{nullptr};
    Text* m_with_spool{nullptr};
};

BedSlicedInfo::BedSlicedInfo(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor),
    m_extruder_rows(std::make_shared<ObservableList<ExtruderRowData>>())
{
    set_object_name("BedEstimates");
    set_min_height(110_fpx);
    set_orientation(Orientation::Vertical);
    set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    set_flags(ImDrawFlags_RoundCornersBottom);
    set_padding(15_fpx);
    set_gap(10_fpx);
    set_visible(false);

    // Always visible, only the content below it scrolls.
    Item* header = emplace_back<Item>();
    header->set_align_items(YGAlignCenter);
    header->set_flex_shrink(0.f);
    Text* title = header->emplace_back<Text>(_u8L("Bed estimates"));
    title->set_font_type(Render::ImguiFontType::Bold);
    title->set_flex_grow(1.f);

    /*// temporary hide unused button
    LayoutButton* calc_btn = header->emplace_back<LayoutButton>("", Render::Icon::Calculator);
    */

    // The panel lives as long as the Preview, so the state persists when switching between
    // Prepare and Preview.
    m_detail_view_toggle = header->emplace_back<ToggleButton>(_u8L("Detail view"));
    m_detail_view_toggle->set_object_name("BedEstimatesDetailView");
    m_detail_view_toggle->set_font_type(Render::ImguiFontType::Bold);
    m_detail_view_toggle->callbacks().checked_changed = [this](bool) { update(); };

    const ImColor secondary_text_color =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);

    // Shrinks (and scrolls) when the panel doesn't get the height of the whole content.
    m_content = emplace_back<ScrollArea>("BedEstimatesContent");
    m_content->set_min_height(40_fpx);
    m_content->set_orientation(Orientation::Vertical);
    m_content->set_gap(10_fpx);
    // Reserves the space of the vertical scrollbar, so it doesn't cover the last column.
    m_content->set_padding(Paddings{0_fpx, 0_fpx, 15_fpx, 0_fpx});
    m_content->set_margin(Margins{0_fpx, 0_fpx, -15_fpx, 0_fpx});

    // A sub-row breaks down the value of the row above it.
    auto add_row = [this, secondary_text_color](
                       const std::string& name,
                       const std::string& label,
                       bool is_sub_row
                   )
    {
        Item* row = add_row_item(m_content, name);
        row->set_visible(false);

        Text* label_text = row->emplace_back<Text>(label);
        label_text->set_width(label_column_width());
        if (is_sub_row) {
            label_text->set_text_color(secondary_text_color);
        } else {
            label_text->set_font_type(Render::ImguiFontType::Bold);
        }
        return row;
    };

    auto add_text_row = [&add_row](
                            const std::string& name,
                            const std::string& label,
                            Text** value,
                            bool is_sub_row
                        )
    {
        Item* row = add_row(name, label, is_sub_row);
        *value    = add_last_cell(row);
        return row;
    };

    auto add_material_row =
        [&add_row](const std::string& name, const std::string& label, bool is_sub_row)
    {
        MaterialRow material_row;
        material_row.row    = add_row(name, label, is_sub_row);
        material_row.weight = add_value_cell(material_row.row);
        material_row.length = add_last_cell(material_row.row);
        return material_row;
    };

    const std::string indent{"   "};

    m_used_material_total =
        add_material_row("BedEstimatesUsedMaterial", _u8L("Used material"), false);
    m_used_material_total.row->set_visible(true);

    m_used_material_for_objects =
        add_material_row("BedEstimatesObjects", indent + _u8L("objects"), true);
    m_used_material_for_wipe_tower =
        add_material_row("BedEstimatesWipeTower", indent + _u8L("wipe tower"), true);
    m_used_material_for_flush =
        add_material_row("BedEstimatesFlush", indent + _u8L("flush"), true);
    m_used_material_for_supports =
        add_material_row("BedEstimatesSupports", indent + _u8L("supports and pad"), true);

    // Single tool only, the per extruder table shows it for more tools. The label has a tooltip,
    // so it isn't a plain text like the labels of the other rows.
    m_used_material_with_spool.row = add_row_item(m_content, "BedEstimatesWithSpool");
    m_used_material_with_spool.row->set_visible(false);
    AbstractButton* with_spool_label = add_with_spool_label(
        m_used_material_with_spool.row,
        indent + _u8L("with spool"),
        "BedEstimatesWithSpoolLabel",
        secondary_text_color
    );
    with_spool_label->set_width(label_column_width());
    m_used_material_with_spool.weight = add_value_cell(m_used_material_with_spool.row);
    // Stays empty, the spool adds weight only.
    m_used_material_with_spool.length = add_last_cell(m_used_material_with_spool.row);

    m_per_extruder_header_row =
        add_row("BedEstimatesPerExtruderHeader", _u8L("Per extruder"), false);
    add_value_cell(m_per_extruder_header_row, _u8L("used"))->set_text_color(secondary_text_color);
    add_value_cell(m_per_extruder_header_row, _u8L("length"))
        ->set_text_color(secondary_text_color);
    m_per_extruder_with_spool_header = add_with_spool_label(
        m_per_extruder_header_row,
        _u8L("with spool"),
        "BedEstimatesWithSpoolHeader",
        secondary_text_color
    );
    // Takes the rest of the row, so the header shrinks with a narrow window.
    m_per_extruder_with_spool_header->set_flex_grow(1);

    m_per_extruder_table = m_content->emplace_back<ExtruderListView>();
    m_per_extruder_table->set_object_name("BedEstimatesPerExtruderTable");
    m_per_extruder_table->set_flex_shrink(0.f);
    m_per_extruder_table->set_orientation(Orientation::Vertical);
    m_per_extruder_table->set_gap(10_fpx);
    m_per_extruder_table->set_visible(false);
    m_per_extruder_table->set_source_list(m_extruder_rows.get());

    add_text_row("BedEstimatesPrintingTime", _u8L("Printing time"), &m_estimated_time, false)
        ->set_visible(true);
    m_estimated_stealth_time_row = add_text_row(
        "BedEstimatesStealthTime",
        indent + _u8L("stealth mode"),
        &m_estimated_stealth_time,
        true
    );
    m_first_layer_time_row =
        add_text_row("BedEstimatesFirstLayer", _u8L("First layer"), &m_first_layer_time, false);
    m_toolchanges_row =
        add_text_row("BedEstimatesToolChanges", _u8L("Tool changes"), &m_toolchanges, false);

    // Hidden for now.
    m_material_cost_row = add_text_row("BedEstimatesCost", _u8L("Cost"), &m_material_cost, false);
}

void BedSlicedInfo::set_used_material(
    MaterialRow& material_row,
    const float weight_g,
    const float length_mm
)
{
    material_row.weight->set_text(format_weight(weight_g));
    material_row.length->set_text(format_length(length_mm));
}

void BedSlicedInfo::update()
{
    const Domain::SlicingId id = m_project_interactor.selected_bed_slicing_id();
    const std::optional<Biz::Slicing::Status> status{
        m_project_interactor.status_cache().get_status(id)
    };

    if (!status || status->code != Biz::Slicing::StatusCode::Finished) {
        set_visible(false);
        return;
    }

    // Without the statistics the panel is hidden, so it doesn't show values of another bed.
    const std::optional<Biz::FDMResultRef> fdm_result{
        m_project_interactor.fdm_result_cache().get_result(id)
    };
    if (fdm_result) {
        const auto* print_statistics =
            std::get_if<Domain::FullPrintStatistics>(&fdm_result->get().print_statistics);
        if (print_statistics) {
            update_fdm(*print_statistics, fdm_result->get().extruder_str_colors);
        }
        set_visible(print_statistics != nullptr);
        return;
    }

    const std::optional<Biz::SLAResultRef> sla_result{
        m_project_interactor.sla_result_cache().get_result(id)
    };
    const bool has_sla_statistics =
        sla_result && sla_result->get().export_data->print_statistics;
    if (has_sla_statistics) {
        update_sla(*sla_result->get().export_data->print_statistics);
    }
    set_visible(has_sla_statistics);
}

void BedSlicedInfo::update_fdm(
    const Domain::FullPrintStatistics& print_statistics,
    const std::vector<std::string>& tool_colors
)
{
    const bool detail_view = m_detail_view_toggle->checked();

    const float weight{print_statistics.total_used_filament_g};
    const float length{print_statistics.total_used_filament_mm};
    set_used_material(m_used_material_total, weight, length);

    const float wipe_tower_weight{print_statistics.total_used_filament_for_wipe_tower_g};
    const float wipe_tower_length{print_statistics.total_used_filament_for_wipe_tower_mm};
    const float flush_weight{print_statistics.total_used_filament_for_flush_g};
    const float flush_length{print_statistics.total_used_filament_for_flush_mm};
    const float objects_weight{weight - wipe_tower_weight - flush_weight};
    const float objects_length{length - wipe_tower_length - flush_length};

    const bool has_wipe_tower{wipe_tower_length > 0.f};
    const bool has_flush{flush_length > 0.f};
    // Without the wipe tower and flush, the objects consumption is the total one.
    const bool has_objects{objects_length > 0.f && (has_wipe_tower || has_flush)};

    set_used_material(m_used_material_for_objects, objects_weight, objects_length);
    m_used_material_for_objects.row->set_visible(detail_view && has_objects);

    set_used_material(m_used_material_for_wipe_tower, wipe_tower_weight, wipe_tower_length);
    m_used_material_for_wipe_tower.row->set_visible(detail_view && has_wipe_tower);

    set_used_material(m_used_material_for_flush, flush_weight, flush_length);
    m_used_material_for_flush.row->set_visible(detail_view && has_flush);

    m_used_material_for_supports.row->set_visible(false);

    std::vector<size_t> used_tools;
    for (size_t tool = 0; tool < print_statistics.used_filament_per_extruder_mm.size(); ++tool) {
        if (print_statistics.used_filament_per_extruder_mm[tool] > 0.f) {
            used_tools.push_back(tool);
        }
    }
    const bool has_multiple_tools{used_tools.size() > 1};

    // The spool weights and the per extruder table are shown in the detail view only.
    const std::vector<float> spool_weights = detail_view ?
        m_project_interactor.project_settings_interactor().get_spool_weights(
            m_project_interactor.selected_project_id(),
            m_project_interactor.selected_config_container_id()
        ) :
        std::vector<float>{};

    const std::optional<float> single_tool_spool_weight = used_tools.size() == 1 ?
        find_spool_weight(spool_weights, used_tools.front()) :
        std::nullopt;
    if (single_tool_spool_weight) {
        m_used_material_with_spool.weight->set_text(
            format_weight(weight + *single_tool_spool_weight)
        );
    }
    m_used_material_with_spool.row->set_visible(detail_view && single_tool_spool_weight);

    const bool show_per_extruder{detail_view && has_multiple_tools};
    if (show_per_extruder) {
        const bool has_spool_weight = std::ranges::any_of(
            used_tools,
            [&spool_weights](const size_t tool)
            { return find_spool_weight(spool_weights, tool).has_value(); }
        );
        update_extruder_rows(
            print_statistics,
            used_tools,
            tool_colors,
            spool_weights,
            has_spool_weight
        );
        m_per_extruder_with_spool_header->set_visible(has_spool_weight);
    }
    m_per_extruder_header_row->set_visible(show_per_extruder);
    m_per_extruder_table->set_visible(show_per_extruder);

    m_estimated_time->set_text(Utils::get_time_dhms(print_statistics.normal_mode_time.time));

    const std::optional<Domain::TimeStatistics>& stealth_time{print_statistics.silent_mode_time};
    if (stealth_time) {
        m_estimated_stealth_time->set_text(Utils::get_time_dhms(stealth_time->time));
    }
    m_estimated_stealth_time_row->set_visible(detail_view && stealth_time);

    m_first_layer_time->set_text(
        Utils::get_time_dhms(print_statistics.estimated_first_layer_printing_time_normal)
    );
    m_first_layer_time_row->set_visible(detail_view);

    m_toolchanges->set_text(std::to_string(print_statistics.total_toolchanges));
    m_toolchanges_row->set_visible(detail_view && print_statistics.total_toolchanges > 0);

    m_material_cost->set_text(format("%1%", print_statistics.total_filament_cost));
}

void BedSlicedInfo::update_sla(const Domain::SLA::PrintStatistics& print_statistics)
{
    const bool detail_view = m_detail_view_toggle->checked();

    // Resin is measured in ml, the statistics are in mm3.
    const auto set_used_resin = [](MaterialRow& material_row, const double volume_mm3)
    {
        material_row.weight->set_text(format("%1$.2f ml", volume_mm3 / 1000.));
        material_row.length->set_text("");
    };
    const double objects_volume{print_statistics.objects_used_material};
    const double supports_volume{print_statistics.support_used_material};
    const bool has_supports{supports_volume > 0.};

    set_used_resin(m_used_material_total, objects_volume + supports_volume);
    set_used_resin(m_used_material_for_objects, objects_volume);
    m_used_material_for_objects.row->set_visible(detail_view && has_supports);
    set_used_resin(m_used_material_for_supports, supports_volume);
    m_used_material_for_supports.row->set_visible(detail_view && has_supports);

    m_used_material_for_wipe_tower.row->set_visible(false);
    m_used_material_for_flush.row->set_visible(false);
    m_used_material_with_spool.row->set_visible(false);
    m_per_extruder_header_row->set_visible(false);
    m_per_extruder_table->set_visible(false);
    m_estimated_stealth_time_row->set_visible(false);
    m_toolchanges_row->set_visible(false);

    m_material_cost->set_text(format("%1%", print_statistics.total_cost));

    const std::vector<double>& layers_times{print_statistics.layers_times_running_total};
    if (!layers_times.empty()) {
        m_first_layer_time->set_text(Utils::get_time_dhms(layers_times.front()));
    }
    m_first_layer_time_row->set_visible(detail_view && !layers_times.empty());

    m_estimated_time->set_text(Utils::get_time_dhms(print_statistics.estimated_print_time));
}

void BedSlicedInfo::update_extruder_rows(
    const Domain::FullPrintStatistics& print_statistics,
    const std::vector<size_t>& used_tools,
    const std::vector<std::string>& tool_colors,
    const std::vector<float>& spool_weights,
    const bool with_spool_visible
)
{
    std::vector<ExtruderRowData> rows;
    rows.reserve(used_tools.size());

    for (const size_t tool : used_tools) {
        ExtruderRowData& row = rows.emplace_back();

        // The same tool colors as the ones the G-code preview uses.
        Domain::ColorRGB color;
        const bool has_color =
            tool < tool_colors.size() && Algorithms::Color::decode_color(tool_colors[tool], color);
        row.color = has_color ? ImColor(color.r(), color.g(), color.b(), 1.f) :
                                m_theme->color_imgui(Platform::Color::AccentPrimary);
        row.extruder_index = tool + 1;

        const float used_weight = tool < print_statistics.used_filament_per_extruder_g.size() ?
            print_statistics.used_filament_per_extruder_g[tool] :
            0.f;
        row.used   = format_weight(used_weight);
        row.length = format_length(print_statistics.used_filament_per_extruder_mm[tool]);

        const std::optional<float> spool_weight = find_spool_weight(spool_weights, tool);
        row.with_spool = spool_weight ? format_weight(used_weight + *spool_weight) : "—";
        row.with_spool_visible = with_spool_visible;
    }

    // Reuses the existing row views, creates or removes only the difference.
    m_extruder_rows->reset(std::move(rows));
}

} // namespace Slic3r::App::ObjectList
