#pragma once

#include "Slic3r/App/Scene/IRenderLayerObject.hpp"

namespace Slic3r::App::Preview {

/**
 * @brief Preview-specific 3D scene layers.
 *
 * Use these to specify layer indices as used by IRenderLayerObject. Each layer can have
 * its layer start/end custom rendering code (e.g. to enable/disable blending, depth-test, etc.).
 */
enum class PreviewSceneLayer : Scene::RenderLayerId
{
    Toolpaths = 0,
    Options,
    CogMarker,
    Bed,
    ToolMarker,
    Shell,
};

} // namespace Slic3r::App::Preview
