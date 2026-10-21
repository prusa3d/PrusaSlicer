#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::Domain {
struct VirtualExtruder;
} // namespace Slic3r::Domain

namespace Slic3r::App::Yoga {
class LayoutButton;
class Rectangle;
class RectangleButton;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

struct RecipeBadgeData
{
    ImColor color;
    unsigned int extruder_id{0};
    int percent{-1};

    bool operator==(const RecipeBadgeData& other) const
    {
        return color.Value == other.color.Value
            && extruder_id == other.extruder_id
            && percent == other.percent;
    }
};

struct RecipeRowData
{
    std::string title;
    std::string subtitle_prefix;
    ImColor swatch_color{0x80, 0x80, 0x80};
    std::vector<RecipeBadgeData> badges;
    std::string warning;
};

class BadgeView : public Biz::DataObserver<RecipeBadgeData>, public Yoga::Item
{
public:
    BadgeView(size_t index, const RecipeBadgeData& data);

protected:
    void on_data_update() override;

private:
    Yoga::Rectangle* m_badge_rect{nullptr};
    Yoga::Text* m_badge_text{nullptr};
    Yoga::Text* m_percent_text{nullptr};
};

/**
 * @brief One row of the virtual extruder list: swatch, title, filament badges, remove button.
 */
class ColorMixRecipeRow : public Yoga::Item, public Biz::DataObserver<RecipeRowData>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    static RecipeRowData make_row_data(
        const Domain::VirtualExtruder& virtual_extruder,
        const std::vector<std::string>& physical_colors,
        const std::vector<std::string>& physical_types
    );

    ColorMixRecipeRow(
        size_t index,
        const RecipeRowData& data,
        FnIndexClicked on_selected,
        FnIndexClicked on_remove_clicked
    );

    void set_selected(bool selected);

protected:
    void on_data_update() override;

private:
    using BadgeListView = Yoga::ListView<BadgeView, RecipeBadgeData>;

    Yoga::RectangleButton* m_main_button{nullptr};
    Yoga::Rectangle* m_swatch_rect{nullptr};
    Yoga::Text* m_title_text{nullptr};
    Yoga::Item* m_subtitle_row{nullptr};
    Yoga::LayoutButton* m_warning_button{nullptr};
    Yoga::LayoutButton* m_remove_button{nullptr};

    Biz::UnsharedPointer<Biz::ObservableList<RecipeBadgeData>> m_badges;
    Yoga::Text* m_subtitle_text{nullptr};
    BadgeListView* m_badge_list_view{nullptr};

    FnIndexClicked m_on_selected;
    FnIndexClicked m_on_remove_clicked;
};

} // namespace Slic3r::App::ColorMix
