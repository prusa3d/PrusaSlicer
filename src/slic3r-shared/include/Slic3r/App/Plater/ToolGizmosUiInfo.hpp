#pragma once
#include "Slic3r/App/Scene/IGizmo.hpp"

namespace Slic3r::App::Plater {

std::string tool_name(Scene::ToolType tool);
std::string tool_shortcut(Scene::ToolType tool);
const char* tool_command_name(Scene::ToolType tool);
Render::Icon tool_icon(Scene::ToolType tool);
Platform::KeyCode tool_key_code(Scene::ToolType tool);

} // namespace Slic3r::App::Plater
