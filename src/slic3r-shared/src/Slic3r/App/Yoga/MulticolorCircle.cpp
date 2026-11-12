#include "Slic3r/App/Yoga/MulticolorCircle.hpp"

namespace Slic3r::App::Yoga {

MulticolorCircle::MulticolorCircle()
{
    set_object_name("MulticolorCircle");
    set_aspect_ratio(1.f);
}

void MulticolorCircle::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImRect rect(to_im(pos), to_im(pos + size));
    if (m_deflate != 0.f) {
        rect.Expand(-m_deflate);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    const ImVec2 center{rect.GetCenter()};
    const float radius{0.5f * rect.GetHeight()};
    constexpr int circle_segments{64};

    ASSERT(!m_colors.empty());
    if (m_colors.size() == 1) {
        ImColor fill_color =
            enabled() ? fill() : (disabled_fill().has_value() ? disabled_fill().value() : fill());
        draw_list->AddCircleFilled(center, radius, fill_color, circle_segments);
    } else {
        const float step{2.0f * IM_PI / static_cast<float>(m_colors.size())};
        constexpr float start_angle{0.5f * IM_PI};
        const int segments_per_sector{
            std::max(2, static_cast<int>((circle_segments + m_colors.size() - 1) / m_colors.size()))
        };

        for (std::size_t i{}; i < m_colors.size(); ++i) {
            const float angle_start{start_angle + static_cast<float>(i) * step};
            const float angle_end{angle_start + step};

            draw_list->PathClear();
            draw_list->PathLineTo(center);
            draw_list->PathArcTo(center, radius, angle_start, angle_end, segments_per_sector);
            draw_list->PathFillConvex(m_colors[i]);
        }
    }

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

void MulticolorCircle::set_colors(const std::vector<ImColor>& colors)
{
    m_colors = colors;
    if (m_colors.size() == 1) {
        set_fill(m_colors.front());
    }
}

float MulticolorCircle::deflate() const
{
    return m_deflate;
}

void MulticolorCircle::set_deflate(float deflate)
{
    m_deflate = deflate;
}

} // namespace Slic3r::App::Yoga
