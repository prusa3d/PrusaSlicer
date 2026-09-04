#pragma once

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {
class Rectangle;
class Text;
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemPreview : public Yoga::Item
{
public:
    explicit ConfigItemPreview();

    void set_data(const Domain::ConfigItem& data, const Domain::ConfigValue& value, bool mixed);
    void set_text_font_type(Render::ImguiFontType font);

private:
    Domain::ConfigItemDef::GUIType m_last_gui_type{Domain::ConfigItemDef::GUIType::undefined};
    std::type_info* m_last_type{nullptr};

    Yoga::Text* m_input_text{nullptr};
    Yoga::Rectangle* m_input_color{nullptr};
    Yoga::ToggleButton* m_input_checkbox{nullptr};
    Render::ImguiFontType m_text_font_type{ Render::ImguiFontType::Regular };
};

} // namespace Slic3r::App
