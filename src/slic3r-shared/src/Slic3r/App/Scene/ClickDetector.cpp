#include "Slic3r/App/Scene/ClickDetector.hpp"

namespace Slic3r::App::Scene {
ClickState ClickDetector::on_mouse(const Platform::MouseEvent& e)
{
    const auto x = e.x();
    const auto y = e.y();
    if (!m_initiated || e.type() == Platform::MouseEvent::Type::ButtonDown) {
        m_initiated = true;
        m_threshold_reached = false;
        m_initial_x = x;
        m_initial_y = y;
        return ClickState::Probing;
    }
    if (e.type() == Platform::MouseEvent::Type::Move) {
        if (m_threshold_reached) {
            return ClickState::Inactive;
        }

        const auto dx = m_initial_x - x;
        const auto dy = m_initial_y - y;
        m_threshold_reached = dx * dx + dy * dy > MAX_ALLOWED_DISTANCE_SQ_PX;
        return m_threshold_reached ? ClickState::Inactive : ClickState::Probing;
    }
    if (e.type() == Platform::MouseEvent::Type::ButtonUp) {
        return m_threshold_reached ? ClickState::Inactive : ClickState::Activated;
    }

    return ClickState::Inactive;
}

void ClickDetector::reset()
{
    m_initiated = false;
    m_threshold_reached = false;
}

} // namespace Slic3r::App::Scene
