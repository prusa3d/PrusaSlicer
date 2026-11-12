#pragma once

#include "Slic3r/App/Yoga/MulticolorCircle.hpp"

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ExtruderIcon : public Yoga::MulticolorCircle
{
public:
    ExtruderIcon();

    void set_extruder_color(const ImColor& color);
    void set_extruders_colors(const std::vector<ImColor>& colors);
    void set_extruder_index(const size_t index);
    void set_extruder_index_visible(bool visible);
    void set_invalid();

protected:
    void enabled_updated_internal() override;

    void update_color();

private:
    ImColor m_fill_collor{IM_COL32_WHITE};
    size_t m_extruder_index{0};
    bool m_extruder_index_visible{false};
    Yoga::Text* m_label{nullptr};
};

} // namespace Slic3r::App
