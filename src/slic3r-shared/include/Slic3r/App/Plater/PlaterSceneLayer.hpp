#pragma once

#include "Slic3r/App/Scene/IRenderLayerObject.hpp"

#include <limits>

namespace Slic3r::App::Plater {

/**
 * @brief Plater-specific 3D scene layers.
 *
 * Use these to specify layer indices as used by IRenderLayerObject. Each layer can have
 * its layer start/end custom rendering code (e.g. to enable/disable blending, depth-test, etc.).
 */
enum class PlaterSceneLayer : Scene::RenderLayerId
{
    DocumentObjects = 0,
    ObjectAccessoriesRegular = 1,
    ObjectAccessoriesOnTop = 2,
    GizmoHandles = 3,
    AlwaysOnTop = std::numeric_limits<int8_t>::max()
};

} // namespace Slic3r::App::Plater
