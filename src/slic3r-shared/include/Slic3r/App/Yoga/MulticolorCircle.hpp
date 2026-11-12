#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

class MulticolorCircle : public Rectangle
{
public:
    MulticolorCircle();

    void render(const Vec2f& pos, const Vec2f& size);

    float deflate() const;
    void set_deflate(float deflate);

protected:
    void set_colors(const std::vector<ImColor>& colors);

private:
    std::vector<ImColor> m_colors{m_theme->color_imgui(Platform::Color::Text)};
    float m_deflate{0.f};
};

} // namespace Slic3r::App::Yoga
