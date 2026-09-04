#include "Slic3r/App/Scene/MouseBindingScheme.hpp"

#include "Slic3r/App/AppServices.hpp"

namespace Slic3r::App::Scene {

using Platform::KeyModifiers;
using Platform::MouseButton;

bool MouseBindingScheme::is_rotate_start(MouseButton button, KeyModifiers modifiers) const
{
    switch (m_scheme) {
    case MouseNavigationScheme::PrusaSlicer:
        return button == MouseButton::Left;
    case MouseNavigationScheme::Tinkercad:
        return button == MouseButton::Right && !Platform::shift_down(modifiers);
    case MouseNavigationScheme::Blender:
        return button == MouseButton::Middle && !Platform::shift_down(modifiers);
    case MouseNavigationScheme::SolidWorks:
        return button == MouseButton::Middle && !Platform::ctrl_down(modifiers);
    case MouseNavigationScheme::Fusion:
        return button == MouseButton::Middle && Platform::shift_down(modifiers);
    default: PANIC();
    }
    return false;
}

bool MouseBindingScheme::is_pan_start(MouseButton button, KeyModifiers modifiers) const
{
    switch (m_scheme) {
    case MouseNavigationScheme::PrusaSlicer:
        return button == MouseButton::Right || button == MouseButton::Middle;
    case MouseNavigationScheme::Tinkercad:
        return button == MouseButton::Middle
               || (button == MouseButton::Right && Platform::shift_down(modifiers));
    case MouseNavigationScheme::Blender:
        return button == MouseButton::Middle && Platform::shift_down(modifiers);
    case MouseNavigationScheme::SolidWorks:
        return button == MouseButton::Middle && Platform::ctrl_down(modifiers);
    case MouseNavigationScheme::Fusion:
        return button == MouseButton::Middle && !Platform::shift_down(modifiers);
    default: PANIC();
    }
    return false;
}

MouseBindingScheme current_mouse_binding_scheme()
{
    return MouseBindingScheme(
        AppServices::instance().app_config().get<MouseNavigationScheme>("mouse_navigation_scheme")
    );
}

} // namespace Slic3r::App::Scene
