#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Plater/MMPaintingUtils.hpp"

namespace Slic3r::App::Yoga {
class Circle;
class TwoColorRing;
}

namespace Slic3r::App::Plater {

class ColorButton : public Yoga::AbstractButton
{
public:
    ColorButton(
        const std::string& label,
        const Domain::ColorRGBA& color,
        ImColor mouse_left_color,
        ImColor mouse_right_color
    );

    std::function<void(SelectedColor)> on_color_selected{[](SelectedColor) {}};

    void select_color(SelectedColor color);

    bool primary_selected() const;

    bool secondary_selected() const;

    void clear_color(SelectedColor color);

protected:
    void hovered_updated_internal() override;

    void action_internal() override;

    void secondary_action_internal() override;

private:
    void update_highlight_circle();
    void update_inner_circle();

private:
    ImColor m_inner_color;
    ImColor m_inner_color_hovered;
    ImColor m_mouse_left_color;
    ImColor m_mouse_right_color;
    Yoga::TwoColorRing* m_highlight_circle{nullptr};
    Yoga::Circle* m_color_circle{nullptr};
    SelectedColor m_selected_color{SelectedColor::None};
};

class ColorSelector : public Yoga::Item
{
public:
    ColorSelector(ImColor mouse_left_color, ImColor mouse_right_color);

    void set_colors(const PaintingPalette& palette);

    void select_color_index(SelectedColor color, std::size_t index);

    std::size_t colors_count() const;

    std::function<void(SelectedColor, std::size_t)> on_color_selected{[](SelectedColor,
                                                                         std::size_t) {}};

private:
    std::vector<ColorButton*> m_selectors;
    ImColor m_mouse_left_color;
    ImColor m_mouse_right_color;
};

} // namespace Slic3r::App::Plater
