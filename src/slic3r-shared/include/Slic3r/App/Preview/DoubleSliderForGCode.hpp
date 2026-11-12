#pragma once

#include "Slic3r/App/Imgui/DoubleSlider.hpp"

namespace Slic3r::App::Preview {

class DoubleSliderForGcode : public Imgui::DoubleSlider::Manager<unsigned int>
{
public:
    explicit DoubleSliderForGcode();

    void set_render_as_disabled(bool value) { m_render_as_disabled = value; }
    bool is_rendering_as_disabled() const { return m_render_as_disabled; }

    void register_commands(
        MenuManager& menu_manager,
        CommandBindingManager& command_binding_manager
    ) override;

private:
    bool m_render_as_disabled{false};
};

} //namespace Slic3r::App::Preview
