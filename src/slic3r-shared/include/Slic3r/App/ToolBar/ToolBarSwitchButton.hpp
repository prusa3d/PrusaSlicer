#pragma once

#include "Slic3r/App/ToolBar/ToolBarButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App {

class ToolBarSwitchButton : public ToolBarButton
{
public:
    enum class SwitchPosition
    {
        Left,
        Center,
        Right,
    };

    ToolBarSwitchButton(
        SwitchPosition switch_position,
        Render::Icon icon,
        const std::string& label   = {},
        const std::string& tooltip = {}
    );

    void set_show_label(bool show_label);

    void style_node() override;
    float width_with_label() const;

private:
    SwitchPosition m_switch_position{SwitchPosition::Left};
    std::string m_label;
    float m_width_with_label = 0.f;
};

} // namespace Slic3r::App
