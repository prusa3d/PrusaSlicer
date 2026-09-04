#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/RevertableControl.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"

namespace Slic3r::App::Yoga {

class InputTextField : public Rectangle, public Yoga::RevertableControl
{
public:
    explicit InputTextField(const std::string& name = {});

    InputText::Callbacks& callbacks();

    /**
     * @note We assume UTF-8 encoding
     */
    const std::string& text() const;
    void set_text(const std::string& text);

    ImGuiInputTextFlags input_flags() const;
    void set_input_flags(ImGuiInputTextFlags flags);

    const std::string& hint() const;
    void set_hint(const std::string& hint);

    const std::string& override_label() const;
    void set_override_label(const std::string& label_override);

    Validator* validator() const;
    void set_validator(std::unique_ptr<Validator> validator);

    void set_tooltip(const std::string& tooltip);
    void set_tooltip_position(Yoga::Position position);

    Render::ImguiFontType font_type() const;
    void set_font_type(Render::ImguiFontType font_type);

    bool hovered() const;

    void set_default(double default_value);
    void set_default(const std::string& default_text);
    bool is_changed_value() const override;
    void reset() override;

    bool resizable() const;
    void set_resizable(bool resizable);

    std::optional<Platform::Color> color() const;
    void set_color(std::optional<Platform::Color> color);

protected:
    virtual void text_updated_internal() {}

    InputText* input_text() const;

private:
    void update_fill();

protected:
    Tooltip* m_tooltip = nullptr;

private:
    InputText* m_input_text{nullptr};
    std::string m_default_text;

    std::optional<Platform::Color> m_color;
};

} // namespace Slic3r::App::Yoga
