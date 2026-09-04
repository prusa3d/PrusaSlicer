#include "Slic3r/App/ToolBar/ToolBarButton.hpp"

#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ContextPopup.hpp"

#include <imgui_internal.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ToolBarButton::ToolBarButton(Render::Icon icon, const std::string& tooltip) :
    LayoutButton(std::string{}, icon, tooltip)
{
    set_background_color(Platform::Color::ButtonTransparent);

    set_content_padding(10.f);
    m_tooltip->set_preferred_position(Position::Bottom);
}

void ToolBarButton::render(const Vec2f& pos, const Vec2f& size)
{
    LayoutButton::render(pos, size);

    // render arrow on top of the children
    if (has_arrow()) {
        ImVec2 button_size = to_im(size);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float arrow_h   = draw_list->_Data->FontSize * 0.35f;

        ImVec2 arrow_size(arrow_h, arrow_h);
        ImVec2 arrow_pos = to_im(pos) + button_size - arrow_size;
        ImRect arrow_bb(arrow_pos, arrow_pos + arrow_size);

        // Draw button background with custom rounding on only one corner
        ImU32 arrow_col = ImGui::GetColorU32(ImGuiCol_Text);

        // draw arrow
        ImVec2 corner_pos = arrow_bb.GetCenter() + ImVec2(1.f, 1.f) * 0.5f * arrow_h;
        draw_list->AddTriangleFilled(
            corner_pos + ImVec2(0.f, -arrow_h),
            corner_pos,
            corner_pos + ImVec2(-arrow_h, 0.f),
            arrow_col
        );
    }
}

void ToolBarButton::style_node()
{
    constexpr float gap = 10;

    if (m_subtoolbar) {
        ASSERT(parent_item());
        if (parent_item()->orientation() == Yoga::Orientation::Vertical) {
            const float our_height     = height();
            const float toolbar_height = m_subtoolbar->height();

            const float top = our_height * 0.5 - toolbar_height * 0.5;

            m_subtoolbar->set_right(-width() - gap);
            m_subtoolbar->set_top(top);
        } else {
            const float our_width     = width();
            const float toolbar_width = m_subtoolbar->width();

            const float left = our_width * 0.5 - toolbar_width * 0.5;

            m_subtoolbar->set_left(left);
            m_subtoolbar->set_top(height() + gap);
        }
    }

    AbstractButton::style_node();
}

Yoga::ContextPopup* ToolBarButton::get_subtoolbar() const
{
    return m_subtoolbar;
}

Yoga::ContextPopup* ToolBarButton::get_or_create_subtoolbar()
{
    if (!m_subtoolbar) {
        m_subtoolbar                  = emplace_back<ContextPopup>("SubToolBar");
        constexpr const float PADDING = 4.f;
        m_subtoolbar->set_padding(PADDING);
        parent_item()->orientation() == Orientation::Horizontal ?
            m_subtoolbar->set_height(40 + PADDING * 2) :
            m_subtoolbar->set_width(40 + PADDING * 2);
        m_subtoolbar->set_position(Position::Bottom);

        m_subtoolbar->callbacks().opened = [this] { set_checked(true); };
        m_subtoolbar->callbacks().closed = [this] { set_checked(false); };

        callbacks().action = [this]
        {
            if (m_subtoolbar) {
                if (m_subtoolbar->opened()) {
                    m_subtoolbar->close();
                } else {
                    m_subtoolbar->open();
                }
            }
        };
    }

    return m_subtoolbar;
}

} // namespace Slic3r::App
