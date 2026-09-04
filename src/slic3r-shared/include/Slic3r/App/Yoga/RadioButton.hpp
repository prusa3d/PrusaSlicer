#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <string>

namespace Slic3r::App::Yoga {

class Circle;
class Text;

class RadioButton : public AbstractButton
{
public:
    explicit RadioButton(const std::string& label, const std::string& tooltip = {});

    Text* label() const;
    void set_label(const std::string& label);
    const std::string& get_label() const;
    void set_font_type(Render::ImguiFontType font_type);

protected:
    void checked_updated_internal() override;
    void enabled_updated_internal() override;
    void hovered_updated_internal() override;
    void update_colors();

private:
    Circle* m_knob{ nullptr };
    Text* m_label{ nullptr };
};

} // namespace Slic3r::App::Yoga
