#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/ColorDropdown.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <functional>
#include <string>
#include <vector>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

using Biz::_u8L;

static Yoga::Item* emplace_icon(
    Yoga::Item* parent,
    Render::Icon icon,
    const Unit& width,
    const Unit& height,
    ImColor color
)
{
    Yoga::Icon* result{parent->emplace_back<Yoga::Icon>(icon)};
    result->set_width(width);
    result->set_height(height);
    result->set_fill_mode(Yoga::Icon::FillMode::PreservedAspectCentered);
    result->set_tint(color);
    return result;
}

class ColorDropdowns : public Yoga::Item
{
public:
    ColorDropdowns(
        Biz::ProjectInteractor& project_interactor,
        const Unit& spacing,
        ImColor mouse_left_color,
        ImColor mouse_right_color
    )
    {
        set_orientation(Yoga::Orientation::Horizontal);
        auto dropdowns{emplace_back<Item>()};
        dropdowns->set_orientation(Yoga::Orientation::Vertical);
        dropdowns->set_flex_grow(1);
        dropdowns->set_gap(spacing);

        auto primary_dropdown_row{dropdowns->emplace_back<Yoga::Item>()};
        primary_dropdown_row->set_gap(spacing);
        primary_dropdown_row->set_align_items(YGAlignCenter);
        emplace_icon(
            primary_dropdown_row,
            Render::Icon::MouseLeft,
            16_fpx, 16_fpx,
            mouse_left_color
        );
        m_primary_dropdown = primary_dropdown_row->emplace_back<Yoga::ColorDropdown>(
            project_interactor,
            false,
            false
        );
        m_primary_dropdown->on_color_selected = [this](std::size_t index)
        { on_color_selected(SelectedColor::Primary, index); };

        auto secondary_dropdown_row{dropdowns->emplace_back<Yoga::Item>()};
        secondary_dropdown_row->set_gap(spacing);
        secondary_dropdown_row->set_align_items(YGAlignCenter);
        emplace_icon(
            secondary_dropdown_row,
            Render::Icon::MouseRight,
            16_fpx, 16_fpx,
            mouse_right_color
        );
        m_secondary_dropdown = secondary_dropdown_row->emplace_back<Yoga::ColorDropdown>(
            project_interactor,
            false,
            false
        );
        m_secondary_dropdown->on_color_selected = [this](std::size_t index)
        { on_color_selected(SelectedColor::Secondary, index); };

        auto switch_area{emplace_back<Item>()};
        switch_area->set_align_items(YGAlign::YGAlignCenter);
        switch_area->set_justify_content(YGJustify::YGJustifyCenter);
        auto switch_button{switch_area->emplace_back<Yoga::LayoutButton>(
            "",
            Render::Icon::Switch,
            _u8L("Switch colors")
        )};
        switch_button->set_content_padding(3_fpx);
        switch_button->set_width(22_fpx);
        switch_button->set_height(22_fpx);
        switch_button->set_margin(Yoga::Margins{spacing, 0, 0, 0});

        switch_button->callbacks().action = [this]()
        {
            switch_colors();
            on_color_selected(SelectedColor::Primary, m_primary_dropdown->current_index());
            on_color_selected(SelectedColor::Secondary, m_secondary_dropdown->current_index());
        };
    }

    void select_color_index(SelectedColor color, std::size_t index)
    {
        if (color == SelectedColor::Primary) {
            m_primary_dropdown->set_current_index(index);
        } else if (color == SelectedColor::Secondary) {
            m_secondary_dropdown->set_current_index(index);
        } else {
            PANIC("Invalid color!");
        }
    }

    void switch_colors()
    {
        const std::size_t primary_index{m_primary_dropdown->current_index()};
        m_primary_dropdown->set_current_index(m_secondary_dropdown->current_index());
        m_secondary_dropdown->set_current_index(primary_index);
    }

    std::pair<std::size_t, std::size_t> current_indicies() const
    {
        return {m_primary_dropdown->current_index(), m_secondary_dropdown->current_index()};
    }

    std::function<void(SelectedColor color, std::size_t index)> on_color_selected{
        [](SelectedColor color, std::size_t index) {}
    };

private:
    Yoga::ColorDropdown* m_primary_dropdown{nullptr};
    Yoga::ColorDropdown* m_secondary_dropdown{nullptr};
};

} // namespace Slic3r::App::Plater
