#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include "Slic3r/App/Yoga/SpinButton.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

SpinButton::SpinButton(ImGuiDir dir) :
    AbstractButton(), //
    m_dir(dir)
{
    // There is no reason to have invalid arrows
    ASSERT(m_dir != ImGuiDir_None && m_dir != ImGuiDir_COUNT);
}

void SpinButton::render(const Vec2f& pos, const Vec2f& size)
{
    AbstractButton::render(pos, size);

    // render arrow
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImU32 text_col      = ImGui::GetColorU32(hovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    YGRenderArrow(window->DrawList, to_im(pos), to_im(size), text_col, m_dir, 1.0f);
}

Vec2f SpinButton::get_item_size()
{
    return Vec2f(1.f, 1.f);
}

} // namespace Slic3r::App::Yoga
