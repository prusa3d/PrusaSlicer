#pragma once

#include "Slic3r/App/Yoga/Namespace.hpp"

#include <string>

namespace Slic3r::App::Yoga {

// parameters for ImGui::Window
struct WindowParams
{
    std::string name_prefix {};
    Vec2f       paddings    { 0.f, 0.f };
};

}
