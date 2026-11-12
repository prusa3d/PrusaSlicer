#pragma once

#include <utility>

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"

namespace Slic3r::App::Scene {

class GizmoKeyEventContext {
public:
    GizmoKeyEventContext(const Platform::KeyboardEvent& keyboard_event)
        : m_keyboard_event(keyboard_event)
    {}

    const Platform::KeyboardEvent& keyboard_event() const { return m_keyboard_event; }

private:
    Platform::KeyboardEvent m_keyboard_event;
};

} // namespace Slic3r::App::Scene
