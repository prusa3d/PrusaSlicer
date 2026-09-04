#include "Slic3r/App/Preview/Legend.hpp"

#include "Slic3r/App/Preview/FdmViewerWrapper.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include <Slic3r/App/libvgcode/FdmViewer.hpp>
#include <Slic3r/App/libvgcode/ColorRange.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <Slic3r/Biz/libpgcode/Utils.hpp>

#include "Slic3r/LegacyFormat.hpp"

#include <boost/nowide/convert.hpp>

#include "Slic3r/LegacyFormat.hpp"

using namespace Slic3r::App::libvgcode;
using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

namespace Slic3r::App::Preview {

static void draw_wrapped_text(const char* text, float text_width)
{
    // When text wrapping is applied, the text gets truncated on the right due to its offset from the parent window.
    // To fix this, we need to increase wrapping_size by ImGui::GetCursorPos().x.
    // Note: This feels like a hack, but a similar use of PushTextWrapPos can be found in imgui_demo.cpp (line 1281).
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + text_width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

static void table_setup_dummy_column(const std::string& table_name)
{
    ImGui::TableSetupColumn(
        ("##dummy_cell_as_padding" + table_name).c_str(),
        ImGuiTableColumnFlags_WidthFixed,
        15.f
    );
}

struct CoarseItem
{
    ColorRGB color;
    std::string text;
    CoarseItem(const ColorRGB& color, const std::string& text) : color(color), text(text) {}
};

using CoarseItems = std::vector<CoarseItem>;

static CoarseItems collect_feature_type_coarse_items(FdmViewer& viewer)
{
    GCodeExtrusionRoles roles = viewer.extrusion_roles();
    CoarseItems ret;
    ret.reserve(roles.size());
    for (GCodeExtrusionRole role : roles) {
        ret.emplace_back(viewer.extrusion_role_color(role), to_string(role));
    }
    return ret;
}

static CoarseItems collect_color_range_coarse_items(FdmViewer& viewer, FdmViewerWrapper& wrapper)
{
    ViewType type = viewer.view_type();
    const ColorRange& range = viewer.color_range(type);
    std::vector<float> values = range.values();

    CoarseItems ret;
    ret.reserve(values.size());
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        std::string txt;
        switch (type)
        {
        case ViewType::Height:
        case ViewType::Width:
        {
            txt = convert_and_format_units(*it, UnitsType::Millimeters,
                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3, true);
            break;
        }
        case ViewType::Speed:
        case ViewType::ActualSpeed:
        {
            txt = convert_and_format_units(*it, UnitsType::MillimetersPerSecond,
                (wrapper.units() == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond, 1, true);
            break;
        }
        case ViewType::VolumetricFlowRate:
        case ViewType::ActualVolumetricFlowRate:
        {
            unsigned int decimals = (wrapper.units() == UnitsSystem::SI) ? 3 : 6;
            txt = convert_and_format_units(*it, UnitsType::MillimetersCubePerSecond,
                (wrapper.units() == UnitsSystem::SI) ? UnitsType::MillimetersCubePerSecond : UnitsType::InchesCubePerSecond, decimals, true);
            break;
        }
        case ViewType::FanSpeed:
        {
            txt = format("%.0f%%", *it);
            break;
        }
        case ViewType::Temperature:
        {
            txt = convert_and_format_units(*it, UnitsType::Celsius,
                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Celsius : UnitsType::Farhenheit, 0, true);
            break;
        }
        case ViewType::LayerTimeLinear:
        case ViewType::LayerTimeLogarithmic:
        {
            txt = format_time_dhms(*it);
            break;
        }
        default:
        {
            txt = "Error";
            break;
        }
        }
        ret.emplace_back(range.color_at(*it), txt);
    }
    return ret;
}

static WipeTowerAndFlushFilamentUsage
get_wipe_tower_and_flush_filament_usage(const FdmViewerWrapper& wrapper, const uint8_t extruder_id)
{
    const WipeTowerAndFlushFilamentUsagePerExtruder& usage_per_extruder{
        wrapper.wipe_tower_and_flush_filament_usage()
    };
    const auto it{usage_per_extruder.find(extruder_id)};

    return (it == usage_per_extruder.end()) ? WipeTowerAndFlushFilamentUsage{} : it->second;
}

static std::string
format_filament_usage(const float length, const float mass, const FdmViewerWrapper& wrapper)
{
    const bool is_si{wrapper.units() == UnitsSystem::SI};
    const std::string formatted_length{convert_and_format_units(
        length,
        UnitsType::Meters,
        is_si ? UnitsType::Meters : UnitsType::Feet,
        2
    )};
    const std::string formatted_mass{convert_and_format_units(
        mass,
        UnitsType::Grams,
        is_si ? UnitsType::Grams : UnitsType::Ounces,
        2
    )};

    return format("%s (%s)", formatted_length.c_str(), formatted_mass.c_str());
}

static CoarseItems collect_tool_coarse_items(FdmViewer& viewer)
{
    const std::vector<uint8_t>& used_extruders_ids = viewer.used_extruders_ids();
    const Palette& tool_colors = viewer.tool_colors();

    CoarseItems ret;
    ret.reserve(used_extruders_ids.size());
    for (size_t i = 0; i < used_extruders_ids.size(); ++i) {
        uint8_t extruder_id = used_extruders_ids[i];
        ret.emplace_back(tool_colors[extruder_id], format("%s %d", _u8L("Extruder").c_str(), 1 + extruder_id));
    }
    return ret;
}

static CoarseItems collect_color_print_coarse_items(FdmViewer& viewer, FdmViewerWrapper& wrapper)
{
    const Palette& color_print_colors = viewer.color_print_colors();
    std::vector<uint8_t> used_extruders_ids = viewer.used_extruders_ids();
    uint8_t extruders_count = viewer.extruders_count();

    CoarseItems ret;
    for (uint8_t id : used_extruders_ids) {
        const ColorPrints& extr_color_prints = viewer.used_extruder_color_prints(id);
        if (extr_color_prints.size() == 1) {
            std::string txt;
            if (extruders_count > 1)
                txt = format("%s %d %s", _u8L("Extruder").c_str(), 1 + id, _u8L("default color"));
            else
                txt = format("%s", _u8L("Default color"));
            ret.emplace_back(color_print_colors[id], txt);
        }
        else {
            size_t counter = 0;
            for (auto it = extr_color_prints.rbegin(); it != extr_color_prints.rend(); ++it) {
                std::string txt;

                if (counter == 0) {
                    if (extruders_count == 1) {
                        txt = format("%s %s", _u8L("above").c_str(),
                            convert_and_format_units(viewer.layer_z(it->layer_id), UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
                                2).c_str());
                    }
                    else {
                        txt = format("%s %d %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                            _u8L("above").c_str(), convert_and_format_units(viewer.layer_z(it->layer_id), UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 2).c_str());
                  }
                }
                else if (counter == extr_color_prints.size() - 1) {
                    if (extruders_count == 1) {
                        txt = format("%s %s", _u8L("up to").c_str(),
                            convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1), UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 2).c_str());
                    }
                    else {
                        txt = format("%s %d %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                            _u8L("up to").c_str(), convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                                UnitsType::Millimeters, (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 2).c_str());
                    }
                }
                else {
                    if (extruders_count == 1) {
                        UnitsType length_units = (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches;
                        std::string txt_from = convert_and_format_units(viewer.layer_z(it->layer_id),
                          UnitsType::Millimeters, length_units, 2, false);
                        std::string txt_to = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                          UnitsType::Millimeters, length_units, 2);
                        txt = format("%s %s %s %s", _u8L("from").c_str(), txt_from.c_str(),
                            _u8L("to").c_str(), txt_to.c_str());
                    }
                    else {
                        UnitsType length_units = (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches;
                        std::string txt_from = convert_and_format_units(viewer.layer_z(it->layer_id),
                          UnitsType::Millimeters, length_units, 2, false);
                        std::string txt_to = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                          UnitsType::Millimeters, length_units, 2);
                        txt = format("%s %d %s %s %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                            _u8L("from").c_str(), txt_from.c_str(), _u8L("to").c_str(), txt_to.c_str());
                    }
                }
                ++counter;

                ret.emplace_back(color_print_colors[it->color_id % color_print_colors.size()], txt);
            }
        }
    }
    return ret;
}

static CoarseItems collect_coarse_items(FdmViewer& viewer, FdmViewerWrapper& wrapper)
{
    ViewType type = viewer.view_type();
    switch (type)
    {
    case ViewType::FeatureType:              { return collect_feature_type_coarse_items(viewer); }
    case ViewType::Height:
    case ViewType::Width:
    case ViewType::ActualSpeed:
    case ViewType::Speed:
    case ViewType::FanSpeed:
    case ViewType::Temperature:
    case ViewType::VolumetricFlowRate:
    case ViewType::ActualVolumetricFlowRate:
    case ViewType::LayerTimeLinear:
    case ViewType::LayerTimeLogarithmic:     { return collect_color_range_coarse_items(viewer, wrapper); }
    case ViewType::Tool:                     { return collect_tool_coarse_items(viewer); }
    case ViewType::ColorPrint:               { return collect_color_print_coarse_items(viewer, wrapper); }
    default:                                 { break; }
    }
    return CoarseItems();
}

static void
draw_item_coarse(CoarseItem& item, const ImVec2& icon_size, float cell_height, float cell_width)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::RenderFrame(
        pos + ImVec2(1.0f, 1.0f),
        pos + icon_size,
        Imgui::to_ImU32(item.color),
        false,
        3.0f
    );
    ImGui::Dummy(icon_size);
    ImGui::SameLine();
    draw_wrapped_text(item.text.c_str(), cell_width - 1.25f * icon_size.x);
}

static void draw_items_coarse(FdmViewer& viewer, FdmViewerWrapper& wrapper)
{
    CoarseItems items = collect_coarse_items(viewer, wrapper);
    float line_height = ImGui::GetTextLineHeight();
    ImVec2 icon_size(line_height, line_height);
    float cell_height = 2.0f * line_height;

    if (ImGui::BeginTable("LegendItems", 3)) {
        ImGui::TableSetupColumn("##Column1_LegendItems", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##Column2_LegendItems", ImGuiTableColumnFlags_WidthStretch);
        table_setup_dummy_column("LegendItems");

        ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.25f * (cell_height - line_height));

        for (size_t i = 0; i < items.size(); i += 2) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            draw_item_coarse(
                items[i],
                icon_size,
                cell_height,
                GImGui->CurrentTable->Columns[0].WidthRequest
            );
            if (i + 1 < items.size()) {
                ImGui::TableSetColumnIndex(1);
                draw_item_coarse(
                    items[i + 1],
                    icon_size,
                    cell_height,
                    GImGui->CurrentTable->Columns[1].WidthRequest
                );
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
}

static void legend_coarse(FdmViewer& viewer, FdmViewerWrapper& wrapper)
{
    draw_items_coarse(viewer, wrapper);
}

static void draw_feature_type_items_detail(
    Render::ImguiRender& imgui_render,
    FdmViewer& viewer,
    FdmViewerWrapper& wrapper,
    const LegendCallbacks& cbs,
    bool show_time_estimate
)
{
    static const ColorRGB PERCENTAGE_COLOR{ 0.56f, 0.56f, 0.56f };
    static constexpr float max_percentage_rect_width = 30.0f;

    GCodeExtrusionRoles roles = viewer.extrusion_roles();
    float inv_total_time = 1.0f / viewer.estimated_time();
    float line_height = ImGui::GetTextLineHeight();
    ImVec2 icon_size(line_height, line_height);
    float cell_height = 1.5f * line_height;

    if (ImGui::BeginTable("FeatureTypeItems", 4, ImGuiTableFlags_SizingFixedFit)) {
        // hide header background
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        bg.w = 0.0f;
        ImGui::PushStyleColor(ImGuiCol_Header, bg);
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, bg);

        ImGui::TableSetupColumn(_u8L("Name").c_str(), ImGuiTableColumnFlags_WidthStretch);
        if (show_time_estimate) {
            ImGui::TableSetupColumn(_u8L("Time").c_str());
        } else {
            ImGui::TableSetupColumn(
                _u8L("Used filament").c_str(),
                ImGuiTableColumnFlags_WidthStretch
            );
        }
        ImGui::TableSetupColumn(_u8L("Percentage").c_str());
        table_setup_dummy_column("FeatureTypeItems");

        ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        ImGui::PopStyleColor(4);

        float max_time_percentage = 0.0f;
        std::vector<std::pair<float, float>> times_percentages;
        float travels_time = 0.0f;
        float travels_time_percentage = 0.0f;
        float wipes_time = 0.0f;
        float wipes_time_percentage = 0.0f;

        float max_length_percentage = 0.0f;
        std::vector<std::pair<float, float>> length_percentages;

        if (show_time_estimate) {
            times_percentages.reserve(roles.size());
            for (size_t i = 0; i < roles.size(); ++i) {
                float time = viewer.extrusion_role_estimated_time(roles[i]);
                float percentage = 100.0f * time * inv_total_time;
                times_percentages.emplace_back(time, percentage);
                max_time_percentage = std::max(max_time_percentage, percentage);
            }
            travels_time = viewer.option_estimated_time(OptionType::Travels);
            travels_time_percentage = 100.0f * travels_time * inv_total_time;
            max_time_percentage = std::max(max_time_percentage, travels_time_percentage);
            wipes_time = viewer.option_estimated_time(OptionType::Wipes);
            wipes_time_percentage = 100.0f * wipes_time * inv_total_time;
            max_time_percentage = std::max(max_time_percentage, wipes_time_percentage);
        }
        else {
            float total_length = 0.0f;
            for (GCodeExtrusionRole role : roles) {
                total_length += viewer.extrusion_role_used_filament_length(role);
            }
            float inv_total_length = 1.0f / total_length;

            length_percentages.reserve(roles.size());
            for (GCodeExtrusionRole role : roles) {
                float length = viewer.extrusion_role_used_filament_length(role);
                float percentage = 100.0f * length * inv_total_length;
                length_percentages.emplace_back(length, percentage);
                max_length_percentage = std::max(max_length_percentage, percentage);
            }
        }

        ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.25f * (cell_height - line_height));

        for (size_t i = 0; i < roles.size(); ++i) {
            const GCodeExtrusionRole& role = roles[i];
            ColorRGB color = viewer.extrusion_role_color(role);
            std::string role_name = to_string(role);
            bool visible = viewer.is_extrusion_role_visible(role);
            uint8_t alpha = visible ? 255 : 127;
            ImVec4 perc_color = { PERCENTAGE_COLOR.r(), PERCENTAGE_COLOR.g(), PERCENTAGE_COLOR.b(), alpha / 255.0f };

            if (!visible)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            ImGui::RenderFrame(row_pos + ImVec2(1.0f, 1.0f), row_pos + icon_size, Imgui::to_ImU32(color, alpha), false, 3.0f);
            ImGui::Dummy(icon_size);
            ImGui::SameLine();
            draw_wrapped_text(role_name.c_str(), GImGui->CurrentTable->Columns[0].ItemWidth);

            // highlight row when hovering and process click on row
            ImGui::SetCursorScreenPos(row_pos);
            if (ImGui::Selectable(("##" + role_name).c_str(), false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                viewer.toggle_extrusion_role_visibility(role);
                if (cbs.cb_extrusion_role_visibility_changed != nullptr)
                    cbs.cb_extrusion_role_visibility_changed();
            }

            if (ImGui::IsItemHovered()) {
                if (!visible) ImGui::PopStyleColor();
                Imgui::tooltip(visible ? _u8L("Click to hide") : _u8L("Click to show"));
                if (!visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }

            ImGui::TableSetColumnIndex(1);
            if (show_time_estimate)
                ImGui::Text("%s", format_time_dhms_short(times_percentages[i].first).c_str());
            else {
                std::string txt_length = convert_and_format_units(length_percentages[i].first,
                    UnitsType::Meters, (wrapper.units() == UnitsSystem::SI) ? UnitsType::Meters : UnitsType::Feet, 2);
                std::string txt_mass = convert_and_format_units(viewer.extrusion_role_used_filament_mass(role),
                    UnitsType::Grams, (wrapper.units() == UnitsSystem::SI) ? UnitsType::Grams : UnitsType::Ounces, 2);
                ImGui::Text("%s (%s)", txt_length.c_str(), txt_mass.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            ImVec2 rect_pos = ImGui::GetCursorScreenPos();
            if (show_time_estimate) {
                float rect_width = max_percentage_rect_width * times_percentages[i].second / max_time_percentage;
                ImGui::RenderFrame(rect_pos + ImVec2(1.0f, 1.0f), rect_pos + ImVec2(rect_width, line_height),
                    ImGui::GetColorU32(perc_color));
                ImGui::Dummy({ max_percentage_rect_width, line_height });
                ImGui::SameLine();
                ImGui::Text("%.1f%%", times_percentages[i].second);
            }
            else {
                float rect_width = max_percentage_rect_width * length_percentages[i].second / max_length_percentage;
                ImGui::RenderFrame(rect_pos + ImVec2(1.0f, 1.0f), rect_pos + ImVec2(rect_width, line_height),
                    ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.0f));
                ImGui::Dummy({ max_percentage_rect_width, line_height });
                ImGui::SameLine();
                ImGui::Text("%.1f%%", length_percentages[i].second);
            }

            if (!visible)
                ImGui::PopStyleColor();
        }

        if (show_time_estimate) {
            std::vector<std::pair<OptionType, std::pair<float, float>>> options = {
                { OptionType::Travels, { travels_time, travels_time_percentage } },
                { OptionType::Wipes,   { wipes_time, wipes_time_percentage } }
            };

            for (const auto& [option, times] : options) {
                const auto& [time, percentage] = times;
                if (viewer.is_option_visible(option)) {
                    ColorRGB color = viewer.option_color(option);
                    std::string name = to_string(option);
                    ImVec4 perc_color = { PERCENTAGE_COLOR.r(), PERCENTAGE_COLOR.g(), PERCENTAGE_COLOR.b(), 1.0f };

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 row_pos = ImGui::GetCursorScreenPos();
                    ImGui::RenderFrame(row_pos + ImVec2(1.0f, 1.0f), row_pos + icon_size, Imgui::to_ImU32(color), false, 3.0f);
                    ImGui::Dummy(icon_size);
                    ImGui::SameLine();
                    ImGui::Text("%s", name.c_str());

                    // highlight row when hovering and process click on row
                    ImGui::SetCursorScreenPos(row_pos);
                    if (ImGui::Selectable(("##" + name).c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        MoveType type = MoveType::COUNT;
                        if (option == OptionType::Travels)
                            type = MoveType::Travel;
                        else if (option == OptionType::Wipes)
                            type = MoveType::Wipe;
                        if (type != MoveType::COUNT)
                            wrapper.set_radius_popup_type(type);
                    }

                    if (ImGui::IsItemHovered())
                       Imgui::tooltip(_u8L("Click to edit thickness"));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", format_time_dhms(time).c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImVec2 rect_pos = ImGui::GetCursorScreenPos();
                    if (show_time_estimate) {
                        float rect_width = max_percentage_rect_width * percentage / max_time_percentage;
                        ImGui::RenderFrame(rect_pos + ImVec2(1.0f, 1.0f), rect_pos + ImVec2(rect_width, line_height),
                            ImGui::GetColorU32(perc_color));
                        ImGui::Dummy({ max_percentage_rect_width, line_height });
                        ImGui::SameLine();
                        ImGui::Text("%.1f%%", percentage);
                    }
                }
            }
        }

        ImGui::PopStyleVar();

        ImGui::EndTable();
    }
}

static void draw_color_range_items_detail(const FdmViewer& viewer, const FdmViewerWrapper& wrapper)
{
    ViewType type = viewer.view_type();
    const ColorRange& range = viewer.color_range(type);
    std::vector<float> values = range.values();
    if (values.empty()) {
        ImGui::Text("%s", _u8L("No data available").c_str());
        return;
    }

    float line_height = ImGui::GetTextLineHeight();
    ImVec2 icon_size(line_height, line_height);
    float cell_height = 1.5f * line_height;

    ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.25f * (cell_height - line_height));
    if (ImGui::BeginTable("ColorRangeItems", 2, ImGuiTableFlags_SizingFixedFit)) {

        for (auto it = values.rbegin(); it != values.rend(); ++it) {
            const ColorRGB& color = range.color_at(*it);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + icon_size, Imgui::to_ImU32(color), false, 3.0f);
            ImGui::Dummy(icon_size);

            ImGui::TableSetColumnIndex(1);
            switch (type)
            {
            case ViewType::Height:
            case ViewType::Width:
            {
                ImGui::Text("%s", convert_and_format_units(*it, UnitsType::Millimeters,
                    (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches, 3, true).c_str());
                break;
            }
            case ViewType::Speed:
            case ViewType::ActualSpeed:
            {
                ImGui::Text("%s", convert_and_format_units(*it, UnitsType::MillimetersPerSecond,
                    (wrapper.units() == UnitsSystem::SI) ? UnitsType::MillimetersPerSecond : UnitsType::InchesPerSecond, 1, true).c_str());
                break;
            }
            case ViewType::VolumetricFlowRate:
            case ViewType::ActualVolumetricFlowRate:
            {
                unsigned int decimals = (wrapper.units() == UnitsSystem::SI) ? 3 : 6;
                ImGui::Text("%s", convert_and_format_units(*it, UnitsType::MillimetersCubePerSecond,
                    (wrapper.units() == UnitsSystem::SI) ? UnitsType::MillimetersCubePerSecond : UnitsType::InchesCubePerSecond, decimals, true).c_str());
                break;
            }
            case ViewType::FanSpeed:
            {
                ImGui::Text("%.0f%%", *it);
                break;
            }
            case ViewType::Temperature:
            {
                ImGui::Text("%s", convert_and_format_units(*it, UnitsType::Celsius,
                    (wrapper.units() == UnitsSystem::SI) ? UnitsType::Celsius : UnitsType::Farhenheit, 0, true).c_str());
                break;
            }
            case ViewType::LayerTimeLinear:
            case ViewType::LayerTimeLogarithmic:
            {
                ImGui::Text("%s", format_time_dhms(*it).c_str());
                break;
            }
            default:
            {
                ImGui::Text("Error");
                break;
            }
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

static void draw_tool_items_details(Render::ImguiRender& imgui_render, const FdmViewer& viewer, const FdmViewerWrapper& wrapper)
{
    const std::vector<uint8_t>& used_extruders_ids = viewer.used_extruders_ids();
    const Palette& tool_colors                     = viewer.tool_colors();
    const bool has_wipe_tower                      = wrapper.has_wipe_tower_filament();
    const bool has_flush                           = wrapper.has_flush_filament();

    float line_height = ImGui::GetTextLineHeight();
    ImVec2 icon_size(line_height, line_height);
    float cell_height = 1.5f * line_height;

    ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.25f * (cell_height - line_height));

    if (ImGui::BeginTable("ToolItems", 2, ImGuiTableFlags_SizingFixedFit)) {

        // hide header background
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        bg.w = 0.0f;
        ImGui::PushStyleColor(ImGuiCol_Header, bg);
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, bg);

        ImGui::TableSetupColumn(_u8L("Extruder").c_str());
        ImGui::TableSetupColumn(_u8L("Used Filament").c_str());
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
        ImGui::TableHeadersRow();
        ImGui::PopFont();

        ImGui::PopStyleColor(4);

        const auto draw_usage_row =
            [&wrapper, icon_size](const std::string& label, const float length, const float mass)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Indent(icon_size.x);
            ImGui::TextUnformatted(label.c_str());
            ImGui::Unindent(icon_size.x);

            ImGui::TableSetColumnIndex(1);
            const std::string text{format_filament_usage(length, mass, wrapper)};
            ImGui::TextUnformatted(text.c_str());
        };

        for (const uint8_t extruder_id : used_extruders_ids) {
            const ColorRGB& color = tool_colors[extruder_id];
            const float extruder_length{viewer.used_extruder_used_filament_length(extruder_id)};
            const float extruder_mass{viewer.used_extruder_used_filament_mass(extruder_id)};

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + icon_size, Imgui::to_ImU32(color));
            ImGui::Dummy(icon_size);
            ImGui::SameLine();
            ImGui::Text("%s %d", _u8L("Extruder").c_str(), 1 + extruder_id);

            ImGui::TableSetColumnIndex(1);
            const std::string text{format_filament_usage(extruder_length, extruder_mass, wrapper)};
            ImGui::TextUnformatted(text.c_str());

            if (has_wipe_tower || has_flush) {
                const WipeTowerAndFlushFilamentUsage usage{
                    get_wipe_tower_and_flush_filament_usage(wrapper, extruder_id)
                };

                draw_usage_row(
                    _u8L("objects"),
                    std::max(0.f, extruder_length - usage.wipe_tower_m - usage.flush_m),
                    std::max(0.f, extruder_mass - usage.wipe_tower_g - usage.flush_g)
                );

                if (has_wipe_tower) {
                    draw_usage_row(_u8L("wipe tower"), usage.wipe_tower_m, usage.wipe_tower_g);
                }

                if (has_flush) {
                    draw_usage_row(_u8L("flush"), usage.flush_m, usage.flush_g);
                }
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

static void draw_color_print_items_detail(Render::ImguiRender& imgui_render, const FdmViewer& viewer, const FdmViewerWrapper& wrapper,
    bool show_time_estimate)
{
    const Palette& color_print_colors = viewer.color_print_colors();
    std::vector<uint8_t> used_extruders_ids = viewer.used_extruders_ids();
    uint8_t extruders_count = viewer.extruders_count();
    float line_height = ImGui::GetTextLineHeight();
    ImVec2 icon_size(line_height, line_height);
    float cell_height = 1.5f * line_height;

    ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0.25f * (cell_height - line_height));

    if (ImGui::BeginTable("ColorPrintItems", 2, ImGuiTableFlags_SizingFixedFit)) {

        for (uint8_t id : used_extruders_ids) {
            const std::vector<ColorPrint>& extr_color_prints = viewer.used_extruder_color_prints(id);
            if (extr_color_prints.size() == 1) {
                const ColorRGB& color = color_print_colors[id];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + icon_size, Imgui::to_ImU32(color), false, 3.0f);
                ImGui::Dummy(icon_size);

                ImGui::TableSetColumnIndex(1);
                if (extruders_count > 1)
                    ImGui::Text("%s %d %s", _u8L("Extruder").c_str(), 1 + id, _u8L("default color").c_str());
                else
                    ImGui::Text("%s", _u8L("Default color").c_str());
            }
            else {
                size_t counter = 0;
                for (auto it = extr_color_prints.rbegin(); it != extr_color_prints.rend(); ++it) {
                    const ColorRGB& color = color_print_colors[it->color_id % color_print_colors.size()];

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + icon_size, Imgui::to_ImU32(color), false, 3.0f);
                    ImGui::Dummy(icon_size);

                    ImGui::TableSetColumnIndex(1);
                    if (counter == 0) {
                        if (extruders_count == 1) {
                            std::string txt = convert_and_format_units(viewer.layer_z(it->layer_id),
                                UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
                                2);
                            ImGui::Text("%s %s", _u8L("above").c_str(), txt.c_str());
                        }
                        else {
                            std::string txt = convert_and_format_units(viewer.layer_z(it->layer_id),
                                UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
                                2);
                            ImGui::Text("%s %d %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                                _u8L("above").c_str(), txt.c_str());
                        }
                    }
                    else if (counter == extr_color_prints.size() - 1) {
                        if (extruders_count == 1) {
                            std::string txt = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                                UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
                                2);
                            ImGui::Text("%s %s", _u8L("up to").c_str(), txt.c_str());
                        }
                        else {
                            std::string txt = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                                UnitsType::Millimeters,
                                (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
                                2);
                            ImGui::Text("%s %d %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                                _u8L("up to").c_str(), txt.c_str());
                        }
                    }
                    else {
                        if (extruders_count == 1) {
                            UnitsType length_units = (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches;
                            std::string txt_from = convert_and_format_units(viewer.layer_z(it->layer_id),
                                UnitsType::Millimeters, length_units, 2, false);
                            std::string txt_to = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                                UnitsType::Millimeters, length_units, 2);
                            ImGui::Text("%s %s %s %s",
                                _u8L("from").c_str(), txt_from.c_str(),
                                _u8L("to").c_str(), txt_to.c_str());
                        }
                        else {
                            UnitsType length_units = (wrapper.units() == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches;
                            std::string txt_from = convert_and_format_units(viewer.layer_z(it->layer_id),
                                UnitsType::Millimeters, length_units, 2, false);
                            std::string txt_to = convert_and_format_units(viewer.layer_z(std::prev(it)->layer_id - 1),
                                UnitsType::Millimeters, length_units, 2);
                            ImGui::Text("%s %d %s %s %s %s", _u8L("Extruder").c_str(), 1 + it->extruder_id,
                                _u8L("from").c_str(), txt_from.c_str(),
                                _u8L("to").c_str(), txt_to.c_str());
                        }
                    }
                    ++counter;
                }
            }
        }

        ImGui::EndTable();
    }

    if (viewer.has_gcode_events_to_show()) {
        ImGui::Dummy({ line_height , line_height });

        if (ImGui::BeginTable(
                "CustomGCodeEvents",
                show_time_estimate ? 5 : 4,
                ImGuiTableFlags_SizingFixedFit
            ))
        {
            // hide header background
            ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            bg.w = 0.0f;
            ImGui::PushStyleColor(ImGuiCol_Header, bg);
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, bg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bg);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, bg);

            table_setup_dummy_column("CustomGCodeEvents_before");
            ImGui::TableSetupColumn(_u8L("Event").c_str());
            if (show_time_estimate) {
                ImGui::TableSetupColumn(_u8L("Duration").c_str());
                ImGui::TableSetupColumn(_u8L("Remaining time").c_str());
            }
            else
                ImGui::TableSetupColumn(_u8L("Used filament").c_str());
            table_setup_dummy_column("CustomGCodeEvents_after");
            ImGui::TableSetupScrollFreeze(0, 1);

            ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            ImGui::PopStyleColor(4);

            std::map<uint8_t, size_t> last_color_id;
            for (uint8_t id : used_extruders_ids) {
                last_color_id[id] = 0;
            }

            enum class EEventType : uint8_t
            {
                Print,
                ColorChange,
                Pause,
                Other
            };

            struct EventItem
            {
                EEventType type;
                uint8_t extruder_id{ 0 };
                std::optional<ColorRGB> color_1;
                std::optional<ColorRGB> color_2;
                std::array<float, 2> times{ 0.0f, 0.0f };
                std::array<float, 2> used_filament{ 0.0f, 0.0f };

                std::string get_label() const {
                    switch (type)
                    {
                    case EEventType::Print:       { return _u8L("Print"); }
                    case EEventType::ColorChange: { return _u8L("Color change"); }
                    case EEventType::Pause:       { return _u8L("Pause"); }
                    default:                      { return _u8L("Error"); }
                    }
                }
            };

            size_t time_mode = size_t(viewer.time_mode());
            const std::vector<GCodeEvent>& events = viewer.gcode_events();
            std::vector<EventItem> event_items;
            uint8_t extruder_id = events.front().extruder_id;
            event_items.push_back({ EEventType::Print, extruder_id,
                color_print_colors[viewer.used_extruder_color_prints(extruder_id)[last_color_id[extruder_id]].color_id] });
            for (size_t i = 0; i < events.size(); ++i) {
                const auto& item = events[i];
                switch (item.type)
                {
                case CustomGCode::Type::PausePrint:
                {
                    auto it = std::find_if(event_items.rbegin(), event_items.rend(), [](const EventItem& e) { return e.type == EEventType::Print; });
                    if (it != event_items.rend())
                        it->times[0] += item.times[time_mode];
                    extruder_id = item.extruder_id;
                    event_items.push_back({ EEventType::Pause, extruder_id, std::nullopt, std::nullopt });
                    event_items.push_back({ EEventType::Print, extruder_id,
                        color_print_colors[viewer.used_extruder_color_prints(extruder_id)[last_color_id[extruder_id]].color_id] });
                    break;
                }
                case CustomGCode::Type::ColorChange:
                {
                    auto it = std::find_if(event_items.rbegin(), event_items.rend(), [](const EventItem& e) { return e.type == EEventType::Print; });
                    if (it != event_items.rend()) {
                        it->times[0] += item.times[time_mode];
                        it->used_filament = item.used_filament;
                    }
                    extruder_id = item.extruder_id;
                    event_items.push_back({ EEventType::ColorChange, extruder_id,
                        color_print_colors[viewer.used_extruder_color_prints(extruder_id)[last_color_id[extruder_id]].color_id],
                        color_print_colors[viewer.used_extruder_color_prints(extruder_id)[last_color_id[extruder_id] + 1].color_id] });
                    ++last_color_id[extruder_id];
                    event_items.push_back({ EEventType::Print, item.extruder_id,
                        color_print_colors[viewer.used_extruder_color_prints(extruder_id)[last_color_id[extruder_id]].color_id] });
                    break;
                }
                default: {
                    auto it = std::find_if(event_items.rbegin(), event_items.rend(), [](const EventItem& e) { return e.type == EEventType::Print; });
                    if (it != event_items.rend())
                        it->times[0] += item.times[time_mode];
                    event_items.push_back({ EEventType::Other, item.extruder_id });
                    break;
                }
                }
            }

            float total_time = viewer.estimated_time();
            float remaining_time = total_time;
            for (auto& item : event_items) {
                item.times[1] = remaining_time;
                remaining_time -= item.times[0];
            }

            size_t max_extruder_id = size_t(*std::max_element(used_extruders_ids.begin(), used_extruders_ids.end()));
            std::vector<std::array<float, 2>> used_filament(1 + max_extruder_id, { 0.0f, 0.0f });
            for (auto& item : event_items) {
                if (item.type == EEventType::Print) {
                    used_filament[item.extruder_id][0] += item.used_filament[0];
                    used_filament[item.extruder_id][1] += item.used_filament[1];
                }
            }

            EventItem& last_event = event_items.back();
            last_event.times[0] = remaining_time;
            last_event.used_filament[0] = viewer.used_extruder_used_filament_length(last_event.extruder_id) - used_filament[last_event.extruder_id][0];
            last_event.used_filament[1] = viewer.used_extruder_used_filament_mass(last_event.extruder_id) - used_filament[last_event.extruder_id][1];

            for (const auto& item : event_items) {
                if (item.type == EEventType::Other)
                    continue;

                ImGui::TableNextRow();

                int column_n = 1;// 0 column is dummy
                ImGui::TableSetColumnIndex(column_n++);
                std::string label = item.get_label();
                ImGui::Text("%s", label.c_str());
                if (item.color_1.has_value()) {
                    ImGui::SameLine();
                    ImVec2 pos              = ImGui::GetCursorScreenPos();
                    ImGui::RenderFrame(
                        pos + ImVec2(1.0f, 1.0f),
                        pos + icon_size,
                        Imgui::to_ImU32(*item.color_1),
                        false,
                        3.0f
                    );
                    ImGui::SameLine();
                    ImGui::Dummy(icon_size);
                    if (item.color_2.has_value()) {
                        pos.x += line_height;
                        ImGui::RenderFrame(pos + ImVec2(1.0f, 1.0f), pos + icon_size, Imgui::to_ImU32(*item.color_2), false, 3.0f);
                        ImGui::SameLine();
                        ImGui::Dummy(icon_size);
                    }
                }

                if (show_time_estimate) {
                    if (item.times[0] > 0.0f) {
                        ImGui::TableSetColumnIndex(column_n++);
                        ImGui::Text("%s", format_time_dhms_short(item.times[0]).c_str());
                    }
                    if (item.times[1] > 0.0f) {
                        ImGui::TableSetColumnIndex(column_n++);
                        ImGui::Text("%s", format_time_dhms_short(item.times[1]).c_str());
                    }
                }
                else {
                    if (item.used_filament[0] > 0.0f) {
                        ImGui::TableSetColumnIndex(column_n++);
                        std::string txt_length = convert_and_format_units(item.used_filament[0],
                            UnitsType::Meters, (wrapper.units() == UnitsSystem::SI) ? UnitsType::Meters : UnitsType::Feet,
                            2);
                        std::string txt_mass = convert_and_format_units(item.used_filament[1],
                            UnitsType::Grams, (wrapper.units() == UnitsSystem::SI) ? UnitsType::Grams : UnitsType::Ounces,
                            2);
                        ImGui::Text("%s (%s)", txt_length.c_str(), txt_mass.c_str());
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar();
}

static void draw_items_detail(Render::ImguiRender& imgui_render, FdmViewer& viewer, FdmViewerWrapper& wrapper, const LegendCallbacks& cbs, bool show_time_estimate)
{
    ViewType type = viewer.view_type();
    switch (type)
    {
    case ViewType::FeatureType:              { draw_feature_type_items_detail(imgui_render, viewer, wrapper, cbs, show_time_estimate); break;}
    case ViewType::Height:
    case ViewType::Width:
    case ViewType::ActualSpeed:
    case ViewType::Speed:
    case ViewType::FanSpeed:
    case ViewType::Temperature:
    case ViewType::VolumetricFlowRate:
    case ViewType::ActualVolumetricFlowRate:
    case ViewType::LayerTimeLinear:
    case ViewType::LayerTimeLogarithmic:     { draw_color_range_items_detail(viewer, wrapper); break;}
    case ViewType::Tool:                     { draw_tool_items_details(imgui_render, viewer, wrapper); break; }
    case ViewType::ColorPrint:               { draw_color_print_items_detail(imgui_render, viewer, wrapper, /*cbs.cb_request_extra_frame, */show_time_estimate); break; }
    default:                                 { break; }
    }
}


static void legend_detail(Render::ImguiRender& imgui_render, libvgcode::FdmViewer& viewer, FdmViewerWrapper& wrapper, const LegendCallbacks& cbs, bool show_time_estimate)
{
    draw_items_detail(imgui_render, viewer, wrapper, cbs, show_time_estimate);
}

Legend::Legend(libvgcode::FdmViewer *viewer, FdmViewerWrapper *wrapper) :
    m_viewer(viewer), m_wrapper(wrapper)
{}

LegendCallbacks& Legend::callbacks()
{
    return m_callback;
}

void Legend::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    render_item_begin(pos, size);

    if (m_wrapper->mode() == FdmViewerWrapperMode::EditorPreGCode ||
        !m_wrapper->has_data()) {
        const std::string msg = _u8L("No data available");
        ImGui::RenderText(to_im(pos), msg.c_str());
    }
    else {
        ImGui::SetCursorScreenPos(to_im(pos));
        if (m_detail_view)
            legend_detail(*m_imgui_render, *m_viewer, *m_wrapper, m_callback, m_show_time_estimate);
        else
            legend_coarse(*m_viewer, *m_wrapper);

        ImVec2 end_pos = ImGui::GetCursorScreenPos();
        Vec2f size(min_width().value, std::max(0.f, end_pos.y - pos.y()));

        if (m_size != size) {
            m_size = size;
            invalidate_min_size_calculation();
        }
    }

    if (m_wrapper->radius_popup_type() != MoveType::COUNT)
        m_wrapper->render_customize_radius_popup();

    render_item_end(pos, size);
}

Vec2f Legend::get_item_size() { return m_size; }

void Legend::set_detail_view(bool detail) { m_detail_view = detail; }

void Legend::set_show_time_estimate(bool show) { m_show_time_estimate = show; }

} // namespace Slic3r::App::Preview
