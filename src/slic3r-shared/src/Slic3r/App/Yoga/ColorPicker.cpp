#include "Slic3r/App/Yoga/ColorPicker.hpp"

namespace Slic3r::App::Yoga {

ColorPicker::Callbacks& ColorPicker::callbacks()
{
    return m_callbacks;
}

ColorPicker::ColorPicker(const std::string& name)
{
    set_object_name(name.empty() ? "ColorPicker" : name);
}

void ColorPicker::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImGui::SetCursorScreenPos(to_im(pos));
    ImGui::BeginChild(
        "##picker_area",
        to_im(ColorPicker::get_item_size()),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(style.ItemSpacing.x, 8.0f)
    ); // vertical gap between items
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemInnerSpacing,
        ImVec2(style.ItemInnerSpacing.x, 6.0f)
    ); // inner gaps some widgets use

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::ColorPicker4(object_name().c_str(), m_data, m_flags)) {
        if (m_callbacks.color_edited) {
            m_callbacks.color_edited(ImColor(m_data[0], m_data[1], m_data[2], m_data[3]));
        }
    }

    ImGui::PopStyleVar(2);

    ImGui::EndChild();

    render_item_end(pos, size);
}

void ColorPicker::set_color(const ImColor& color)
{
    m_data[0] = color.Value.x;
    m_data[1] = color.Value.y;
    m_data[2] = color.Value.z;
    m_data[3] = color.Value.w;
}

ImGuiColorEditFlags ColorPicker::flags() const
{
    return m_flags;
}

void ColorPicker::set_flags(ImGuiColorEditFlags flags)
{
    if (m_flags != flags) {
        m_flags = flags;
        set_style_dirty();
    }
}

Vec2f ColorPicker::get_item_size()
{
    return {200, 240};
}

} // namespace Slic3r::App::Yoga
