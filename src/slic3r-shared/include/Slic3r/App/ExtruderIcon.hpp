#pragma once

#include "Slic3r/App/Yoga/Circle.hpp"

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ExtruderIcon : public Yoga::Circle
{
public:
    ExtruderIcon();

    void set_extruder_color(const ImColor& color);
    void set_extruder_index(const size_t index);

protected:
    void enabled_updated_internal() override;

    void update_color();

private:
    ImColor m_fill_collor{IM_COL32_WHITE};
    size_t m_extruder_index{0};
    Yoga::Text* m_label{nullptr};
};

} // namespace Slic3r::App
