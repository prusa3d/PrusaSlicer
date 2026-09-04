#pragma once

#include "Slic3r/App/Platform/MouseEvent.hpp"

namespace Slic3r::App::Scene {

enum class ClickState
{
    Probing,
    Activated,
    Inactive
};

class ClickDetector
{
public:
    ClickState on_mouse(const Platform::MouseEvent& e);
    void reset();

    constexpr static int MAX_ALLOWED_DISTANCE_PX{4};
    constexpr static int MAX_ALLOWED_DISTANCE_SQ_PX{
        MAX_ALLOWED_DISTANCE_PX * MAX_ALLOWED_DISTANCE_PX
    };

private:
    bool m_initiated{false}, m_threshold_reached{false};
    int m_initial_x{-1}, m_initial_y{-1};

};
}
