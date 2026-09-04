#include "Slic3r/App/Yoga/LambdaItem.hpp"

namespace Slic3r::App::Yoga {

LambdaItem::LambdaItem(RenderPosFn render_fn) : Item(), m_render_fn(render_fn) {
    set_object_name("LambdaItem");
}

void LambdaItem::render(const Vec2f& pos, const Vec2f& size) {
    render_item_begin(pos, size);

    ImGui::SetCursorScreenPos(to_im(pos));
    m_render_fn(pos, size);

    render_item_end(pos, size);
}

} // namespace Slic3r::App::Yoga
