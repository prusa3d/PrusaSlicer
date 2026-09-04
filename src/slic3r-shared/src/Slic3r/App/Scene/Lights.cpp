#include "Slic3r/App/Scene/Lights.hpp"
#include "Slic3r/App/Render/Material.hpp"

namespace Slic3r::App::Scene {

std::string to_string(LightReferenceSystem sys)
{
    switch (sys)
    {
    case LightReferenceSystem::Camera: { return "Camera"; }
    case LightReferenceSystem::World:  { return "World"; }
    default:                           { return "Unknown"; }
    }
}

} // namespace Slic3r::App::Scene
