#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Plater/LayerHeightProfileControl.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
class Rectangle;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class HeightRangeButton;

class HeightRangeRow : public Yoga::Item
{
public:
    struct Callbacks
    {
        std::function<void()> delete_clicked = []() {};
        std::function<void()> undo_clicked   = []() {};
        std::function<void()> selected       = []() {};
        std::function<void(bool)> hovered    = [](bool) {};
    };

    explicit HeightRangeRow(const HeightRangeEntry& height_range);

    Callbacks& callbacks();

    const HeightRangeEntry& height_range() const;

    void set_height_range(const HeightRangeEntry& height_range);
    void set_has_overrides(bool has_overrides);
    void set_checked(bool checked);
    void set_highlighted(bool highlighted);

private:
    HeightRangeEntry m_height_range;
    bool m_has_overrides{false};

    HeightRangeButton* m_height_range_button{nullptr};
    Yoga::LayoutButton* m_undo_button{nullptr};
    Yoga::LayoutButton* m_delete_button{nullptr};

    Callbacks m_callbacks;

    void update_labels();
    void update_undo_button();
};

} // namespace Slic3r::App::Plater
