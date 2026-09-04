#pragma once

#define ENABLED_DEBUG_OUTLINE 0
#define ENABLED_SHORTCUTS_LIST 0

#if ENABLED_DEBUG_OUTLINE
namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene
#endif // ENABLED_DEBUG_OUTLINE

#if ENABLED_SHORTCUTS_LIST
namespace Slic3r::App::Platform {
class CommandRegistry;
} // namespace Slic3r::App::Scene
#endif // ENABLED_DEBUG_OUTLINE

namespace Slic3r::App {

#if ENABLED_DEBUG_OUTLINE
void imgui_scenegraph_node_info(const Scene::Node& node);
#endif // ENABLED_DEBUG_OUTLINE

#if ENABLED_SHORTCUTS_LIST
void imgui_shortcuts_list(Platform::CommandRegistry& command_registry);
#endif

} // namespace Slic3r::App
