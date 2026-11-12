#include "Slic3r/App/Plater/KeyIcon.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

KeyIcon::KeyIcon(const std::string& label)
{
    set_fill(m_theme->color_imgui(Platform::Color::Transparent));
    set_rounding(4);
    set_border_width(1);
    set_border_color(m_theme->color_imgui(Platform::Color::Text));
    set_padding(Paddings{8, 2, 8, 3}); // Text is not centered, bottom padding has to be bigger

    m_text = emplace_back<Text>(label);
    m_text->set_font_size(14);
}

void KeyIcon::set_tint(const ImColor &color)
{
    set_border_color(color);
    m_text->set_text_color(color);
}

} // namespace Slic3r::App::Plater
