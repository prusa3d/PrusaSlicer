#include "Slic3r/App/Yoga/Circle.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r::App::Yoga {

Circle::Circle() : Rectangle()
{
    set_object_name("Circle");
    set_aspect_ratio(1.f);
}

void Circle::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImRect rect(to_im(pos), to_im(pos + size));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    ImColor fill_color =
        enabled() ? fill() : (disabled_fill().has_value() ? disabled_fill().value() : fill());

    draw_list->AddCircleFilled(rect.GetCenter(), 0.5f * rect.GetHeight(), fill_color, 36);
    if (border_width() > 0) {
        draw_list->AddCircle(
            rect.GetCenter(),
            0.5f * rect.GetHeight(),
            enabled() ? border_color() : border_color_disabled(),
            36,
            border_width()
        );
    }

    render_item_end(pos, size);
}

} // namespace Slic3r::App::Yoga
