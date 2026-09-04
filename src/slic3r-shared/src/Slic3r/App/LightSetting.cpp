#include "Slic3r/App/LightSetting.hpp"

#include "Slic3r/App/Scene/Lights.hpp"

static Slic3r::App::Scene::Lighting g_lighting;

namespace Slic3r::App {

const Slic3r::App::Scene::Lighting& global_lighting()
{
    return g_lighting;
}

void set_global_lighting(const Slic3r::App::Scene::Lighting& lighting)
{
    g_lighting = lighting;
}

} // namespace Slic3r::App
