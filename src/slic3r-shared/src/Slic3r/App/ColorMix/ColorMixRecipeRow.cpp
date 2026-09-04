#include "Slic3r/App/ColorMix/ColorMixRecipeRow.hpp"

#include "Slic3r/App/ColorMix/ColorMixUtils.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

using Slic3r::App::Platform::Color;
using Slic3r::App::Platform::ColorGroup;
using Slic3r::App::Render::Icon;
using Slic3r::App::Render::ImguiFontType;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderGradientStop;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::ColorMix {

ColorMixRecipeRow::RowData ColorMixRecipeRow::make_row_data(
    const VirtualExtruder& virtual_extruder,
    const std::vector<std::string>& physical_colors
)
{
    RowData row_data;
    row_data.title        = recipe_title(virtual_extruder);
    row_data.swatch_color = parse_hex_color(effective_color_hex(virtual_extruder, physical_colors));

    if (virtual_extruder.type() == VirtualExtruder::Type::Gradient) {
        row_data.subtitle_prefix = _u8L("Gradient");
        if (virtual_extruder.gradient.has_value()) {
            row_data.badges.reserve(virtual_extruder.gradient->stops.size());
            for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops) {
                row_data.badges.push_back(
                    {physical_slot_color(physical_colors, stop.extruder_id), stop.extruder_id, -1}
                );
            }
        }

        return row_data;
    }

    row_data.badges.reserve(virtual_extruder.components.size());
    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
        row_data.badges.push_back(
            {physical_slot_color(physical_colors, component.extruder_id),
             component.extruder_id,
             int(std::round(component.ratio * 100.0))}
        );
    }

    return row_data;
}

ColorMixRecipeRow::ColorMixRecipeRow(const RowData& row_data)
{
    constexpr const Unit RowHeight{52_fpx};
    constexpr const Unit SwatchSize{30_fpx};

    this->set_object_name("ColorMixRecipeRow");
    this->set_orientation(Orientation::Horizontal);
    this->set_height(RowHeight);
    this->set_flex_shrink(0);

    m_main_button = this->emplace_back<RectangleButton>();
    m_main_button->set_flex_grow(1.f);
    m_main_button->set_height(RowHeight);
    m_main_button->set_background_color(Color::ButtonTransparent);
    m_main_button->set_background_color_checked(Color::Button);
    m_main_button->set_rounding(6.f);
    m_main_button->set_allow_overlap(true);
    m_main_button->set_content_orientation(Orientation::Horizontal);
    m_main_button->set_content_align_items(YGAlignCenter);
    m_main_button->set_content_padding({8_fpx, 4_fpx});
    m_main_button->callbacks().action = [this]()
    {
        if (m_callbacks.selected) {
            m_callbacks.selected();
        }
    };

    m_swatch_rect = m_main_button->emplace_back<Rectangle>();
    m_swatch_rect->set_width(SwatchSize);
    m_swatch_rect->set_height(SwatchSize);
    m_swatch_rect->set_flex_shrink(0);
    m_swatch_rect->set_rounding(6.f);
    m_swatch_rect->set_border_width(1.f);
    m_swatch_rect->set_border_color(m_theme->color_imgui(Color::Text, ColorGroup::Disabled));

    Item* text_column = m_main_button->emplace_back<Item>();
    text_column->set_orientation(Orientation::Vertical);
    text_column->set_flex_grow(1.f);
    text_column->set_margin({8_fpx, 0, 0, 0});
    text_column->set_gap(1_fpx);

    m_title_text = text_column->emplace_back<Text>("", ImguiFontType::Bold);

    m_subtitle_row = text_column->emplace_back<Item>();
    m_subtitle_row->set_orientation(Orientation::Horizontal);
    m_subtitle_row->set_align_items(YGAlignCenter);
    m_subtitle_row->set_gap(6_fpx);

    m_remove_button = m_main_button->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::DeleteBtnIcon,
        _u8L("Remove this virtual extruder.")
    );
    m_remove_button->set_width(22_fpx);
    m_remove_button->set_height(22_fpx);
    m_remove_button->set_self_align(YGAlignCenter);
    m_remove_button->set_flex_shrink(0);
    m_remove_button->set_margin({4_fpx, 0, 0, 0});
    m_remove_button->set_background_color(Color::ButtonTransparent);
    m_remove_button->callbacks().action = [this]()
    {
        if (m_callbacks.remove_clicked) {
            m_callbacks.remove_clicked();
        }
    };

    this->update(row_data);
}

void ColorMixRecipeRow::set_selected(bool selected)
{
    m_main_button->set_checked(selected);
}

void ColorMixRecipeRow::update(const RowData& row_data)
{
    m_title_text->set_text(row_data.title);
    m_swatch_rect->set_fill(row_data.swatch_color);
    this->rebuild_subtitle(row_data);
}

void ColorMixRecipeRow::rebuild_subtitle(const RowData& row_data)
{
    clear_children_later(m_subtitle_row);

    const ImColor subtitle_color = m_theme->color_imgui(Color::Text, ColorGroup::Disabled);

    const auto add_subtitle_text = [&](const std::string& text)
    {
        Text* subtitle_text = m_subtitle_row->emplace_back<Text>(text);
        subtitle_text->set_text_color(subtitle_color);
    };

    if (row_data.badges.empty()) {
        if (!row_data.plain_subtitle.empty()) {
            add_subtitle_text(row_data.plain_subtitle);
        }

        return;
    }

    if (!row_data.subtitle_prefix.empty()) {
        add_subtitle_text(row_data.subtitle_prefix + " -");
    }

    for (const BadgeData& badge : row_data.badges) {
        Rectangle* badge_rect = m_subtitle_row->emplace_back<Rectangle>();
        badge_rect->set_fill(badge.color);
        badge_rect->set_rounding(2.f);
        badge_rect->set_border_width(1.f);
        badge_rect->set_border_color(subtitle_color);
        badge_rect->set_width(18_fpx);
        badge_rect->set_height(18_fpx);
        badge_rect->set_flex_shrink(0);
        badge_rect->set_justify_content(YGJustifyCenter);
        badge_rect->set_align_items(YGAlignCenter);

        Text* badge_text =
            badge_rect->emplace_back<Text>(std::to_string(badge.extruder_id), ImguiFontType::Bold);
        badge_text->set_font_size(0.85_rem);
        badge_text->set_text_color(Imgui::contrast_color(badge.color));

        if (badge.percent >= 0) {
            add_subtitle_text(fmt::format("{}%", badge.percent));
        }
    }
}

} // namespace Slic3r::App::ColorMix
