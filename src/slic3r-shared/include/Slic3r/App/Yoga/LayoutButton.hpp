#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class Icon;

class LayoutButton : public RectangleButton
{
public:
    LayoutButton(const std::string& label);
    LayoutButton(const std::string& label, Render::Icon icon);
    LayoutButton(const std::string& label, Render::Icon icon, const std::string& tooltip);

    const std::string& label() const;
    void set_label(const std::string& label);

    Render::ImguiFontType label_font_type() const;
    void set_label_font_type(Render::ImguiFontType label_font_type);
    void set_expand_label(bool expand);
    const ImColor& label_color() const;
    void set_label_color(const ImColor& color);

    Render::Icon icon() const;
    void set_icon(Render::Icon icon);
    ImColor icon_tint() const;
    void set_icon_tint(const ImColor& tint);
    Text::WrapMode text_wrap_mode() const;
    void set_text_wrap_mode(Text::WrapMode wrap_mode);

    Text* label_object() const;
    Icon* icon_object() const;

private:
    Icon* m_icon = nullptr;
    Text* m_text = nullptr;
};

} // namespace Slic3r::App::Yoga
