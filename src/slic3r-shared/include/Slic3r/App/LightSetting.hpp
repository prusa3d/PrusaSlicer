#pragma once

namespace Slic3r::App::Scene {
struct Lighting;
} // namespace Slic3r::App::Scene

namespace Slic3r::App {

const Slic3r::App::Scene::Lighting& global_lighting();
void set_global_lighting(const Slic3r::App::Scene::Lighting& lighting);

} // namespace Slic3r::App
