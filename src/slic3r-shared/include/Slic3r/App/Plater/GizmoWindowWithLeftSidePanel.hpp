#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Yoga {
class Text;
class Rectangle;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

/**
 * @brief A GizmoWindow variant that supports a side panel on the left.
 */
class GizmoWindowWithLeftSidePanel : public GizmoWindow
{
public:
    explicit GizmoWindowWithLeftSidePanel();

    Yoga::Rectangle* side_panel() const;
    Yoga::Text* side_panel_header_title() const;

private:
    Yoga::Rectangle* m_side_panel         = nullptr;
    Yoga::Text* m_side_panel_header_title = nullptr;
};

} // namespace Slic3r::App::Plater
