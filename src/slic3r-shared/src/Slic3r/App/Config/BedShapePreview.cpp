#include "Slic3r/App/Config/BedShapePreview.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

#include <imgui/imgui_internal.h>

static const ImColor RED{255, 0, 0};
static const ImColor GREEN{0, 255, 0};

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

BedShapePreview::BedShapePreview()
{
    set_object_name("BedShapePreview");
}

void BedShapePreview::set_shape(
    const std::vector<Domain::Vec2d>& points,
    const std::vector<Domain::Vec2d>& triangles,
    const Domain::Vec2d& orig_pos
)
{
    m_points    = points;
    m_triangles = triangles;
    m_orig_pos  = orig_pos;
    m_fill      = m_theme->color_imgui(Platform::Color::WindowBgAlternate);
    m_disabled_fill =
        m_theme->color_imgui(Platform::Color::WindowBgAlternate, Platform::ColorGroup::Disabled);
    m_border_fill = m_theme->color_imgui(Platform::Color::Text);
    m_disabled_border_fill =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
}

const ImColor& BedShapePreview::shape_fill() const
{
    return m_shape_fill;
}

void BedShapePreview::set_shape_fill(const ImColor& fill)
{
    m_shape_fill = fill;
}

void BedShapePreview::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (m_points.empty() || size.x() <= 0 || size.y() <= 0)
        return;

    const ImColor fill_color        = enabled() ? m_fill : m_disabled_fill;
    const ImColor border_fill_color = enabled() ? m_border_fill : m_disabled_border_fill;

    using namespace Slic3r::Biz::Algorithms::BoundingBox;

    // Bounding boxes (model space)

    BoundingBoxf canvas_bb(Domain::Vec2d::Zero(), size.cast<double>());
    Domain::Vec2d canvas_center = center(canvas_bb);

    // Domain Vec2

    BoundingBoxf bb = construct(m_points);
    bb              = merge(bb, m_orig_pos); // ensure origin visible

    Domain::Vec2d bb_size    = sizes(bb);
    Domain::Vec2d bed_center = center(bb);

    double scale = std::min(size.x() / bb_size.x(), size.y() / bb_size.y());

    Domain::Vec2d shift{
        canvas_center.x() - bed_center.x() * scale,
        canvas_center.y() - bed_center.y() * scale
    };

    // Background

    dl->AddRectFilled(to_im(pos), ImVec2(pos.x() + size.x(), pos.y() + size.y()), fill_color);

    auto point_to_render_pos = [&](const Domain::Vec2d& p) -> ImVec2
    { // model -> scaled -> shifted -> ImGui
        return ImVec2(
            pos.x() + static_cast<float>(p.x() * scale + shift.x()),
            pos.y() + static_cast<float>(size.y() - (p.y() * scale + shift.y()))
        );
    };

    std::vector<ImVec2> poly;
    poly.reserve(m_points.size());
    for (const Domain::Vec2d& p : m_points) {
        poly.emplace_back(point_to_render_pos(p));
    }

    // Bed fill

    if (m_triangles.empty()) {
        dl->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()), m_shape_fill);
    } else {
        for (size_t i = 0; i < m_triangles.size(); i += 3) {
            dl->AddTriangleFilled(
                point_to_render_pos(m_triangles[i]),
                point_to_render_pos(m_triangles[i + 1]),
                point_to_render_pos(m_triangles[i + 2]),
                m_shape_fill
            );
        }
    }

    // Grid (1 cm)

    constexpr double step = 10.0;

    for (double x = bb.min.x() - fmod(bb.min.x(), step); x < bb.max.x(); x += step) {
        ImVec2 a = point_to_render_pos({x, bb.min.y()});
        ImVec2 b = point_to_render_pos({x, bb.max.y()});
        dl->AddLine(a, b, fill_color, 1.0f);
    }

    for (double y = bb.min.y() - fmod(bb.min.y(), step); y < bb.max.y(); y += step) {
        ImVec2 a = point_to_render_pos({bb.min.x(), y});
        ImVec2 b = point_to_render_pos({bb.max.x(), y});
        dl->AddLine(a, b, fill_color, 1.0f);
    }

    // Bed contour

    dl->AddPolyline(poly.data(), static_cast<int>(poly.size()), border_fill_color, true, 1.0f);

    // Axes

    ImVec2 origin             = point_to_render_pos({0, 0});
    constexpr float axes_len  = 50.f;
    constexpr float arrow_len = 6.f;

    // X (red)
    ImVec2 x_end(origin.x + axes_len, origin.y);
    dl->AddLine(origin, x_end, ImColor{255, 0, 0}, 2.0f);
    dl->AddLine(x_end, ImVec2(x_end.x - arrow_len, x_end.y - arrow_len), RED, 2.0f);
    dl->AddLine(x_end, ImVec2(x_end.x - arrow_len, x_end.y + arrow_len), RED, 2.0f);

    // Y (green)
    ImVec2 y_end(origin.x, origin.y - axes_len);
    dl->AddLine(origin, y_end, GREEN, 2.0f);
    dl->AddLine(y_end, ImVec2(y_end.x - arrow_len, y_end.y + arrow_len), GREEN, 2.0f);
    dl->AddLine(y_end, ImVec2(y_end.x + arrow_len, y_end.y + arrow_len), GREEN, 2.0f);

    // Origin marker + label

    dl->AddCircleFilled(origin, 3.0f, border_fill_color);
    dl->AddText(ImVec2(origin.x + 3, origin.y + 3), border_fill_color, "(0,0)");

    render_item_end(pos, size);
}

} // namespace Slic3r::App
