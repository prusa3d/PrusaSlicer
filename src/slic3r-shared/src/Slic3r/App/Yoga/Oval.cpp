#include "Slic3r/App/Yoga/Oval.hpp"

namespace Slic3r::App::Yoga {

Oval::Oval() : Rectangle() {
    set_object_name("Oval");
}

void Oval::render(const Vec2f& pos, const Vec2f& size)
{
    set_rounding(0.5f * std::min(size.x(), size.y()));
    Rectangle::render(pos, size);
}

} // namespace Slic3r::App::Yoga