
#include "Slic3r/App/Yoga/Separator.hpp"

namespace Slic3r::App::Yoga {

Separator::Separator(Orientation orientation) : Rectangle()
{
    // We are abusing orientation attribute from Item, it doesnt matter
    // as we do not expect separator to actully have any children
    set_orientation(orientation);
    orientation == Orientation::Horizontal ? set_height(1) : set_width(1);
    if (orientation == Orientation::Horizontal) {
        set_max_height(1);
    } else {
        set_max_width(1);
    }
    set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    set_rounding(0);
    set_object_name("Separator");
}

Vec2f Separator::get_item_size()
{
    return orientation() == Orientation::Horizontal ? Vec2f{0, 1} : Vec2f{1, 0};
}

} // namespace Slic3r::App::Yoga
