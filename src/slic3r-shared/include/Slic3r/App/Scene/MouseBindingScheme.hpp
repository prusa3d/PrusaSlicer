#pragma once

#include "Slic3r/App/Platform/MouseEvent.hpp"
#include "Slic3r/App/AppConfig.hpp"

namespace Slic3r::App::Scene {

// Answers semantic navigation questions ("does this chord start an orbit/pan?") for the
// user's chosen mouse navigation scheme, based on the real physical button and modifiers.
// Gizmos that care about navigation query this directly instead of the input stream being
// rewritten for everyone, which would risk colliding with unrelated uses of the same
// button/modifier combinations elsewhere (e.g. Shift already means something else to
// BedSelectGizmo and several tool gizmos).
class MouseBindingScheme
{
public:
    explicit MouseBindingScheme(MouseNavigationScheme scheme) : m_scheme(scheme) {}

    bool is_prusaslicer() const { return m_scheme == MouseNavigationScheme::PrusaSlicer; }

    bool is_rotate_start(Platform::MouseButton button, Platform::KeyModifiers modifiers) const;
    bool is_pan_start(Platform::MouseButton button, Platform::KeyModifiers modifiers) const;

private:
    MouseNavigationScheme m_scheme;
};

// Reads the currently selected scheme from AppConfig.
MouseBindingScheme current_mouse_binding_scheme();

} // namespace Slic3r::App::Scene
