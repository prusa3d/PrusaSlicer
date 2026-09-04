#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::App::Yoga {
class LayoutButton;
class Rectangle;
class RectangleButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

/**
 * @brief One row of the virtual extruder list: swatch, title, filament badges, remove button.
 */
class ColorMixRecipeRow : public Yoga::Item
{
public:
    struct BadgeData
    {
        ImColor color;
        unsigned int extruder_id{0};
        int percent{-1};
    };

    struct RowData
    {
        std::string title;
        std::string subtitle_prefix;
        std::string plain_subtitle;
        ImColor swatch_color{0x80, 0x80, 0x80};
        std::vector<BadgeData> badges;
    };

    struct Callbacks
    {
        std::function<void()> selected{nullptr};
        std::function<void()> remove_clicked{nullptr};
    };

    static RowData make_row_data(
        const Domain::VirtualExtruder& virtual_extruder,
        const std::vector<std::string>& physical_colors
    );

    explicit ColorMixRecipeRow(const RowData& row_data);

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

    void set_selected(bool selected);

    void update(const RowData& row_data);

private:
    void rebuild_subtitle(const RowData& row_data);

    Yoga::RectangleButton* m_main_button{nullptr};
    Yoga::Rectangle* m_swatch_rect{nullptr};
    Yoga::Text* m_title_text{nullptr};
    Yoga::Item* m_subtitle_row{nullptr};
    Yoga::LayoutButton* m_remove_button{nullptr};

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::ColorMix
