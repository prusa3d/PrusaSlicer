#pragma once

namespace Slic3r::App::Plater {

constexpr std::chrono::milliseconds GIZMO_SELECTION_TIMEOUT{150};
constexpr std::chrono::milliseconds GIZMO_CONTEXT_MENU_TIMEOUT{175};

static_assert(GIZMO_SELECTION_TIMEOUT < GIZMO_CONTEXT_MENU_TIMEOUT, "Selection needs to be handled before the context menu is shown, not after");

}