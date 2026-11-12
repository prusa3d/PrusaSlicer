#pragma once

#include <cstdint>

namespace Slic3r::App::Scene {

using RenderLayerId = int8_t;

/**
 * @brief Interface for an object placed to render layer.
 *
 * A render layer is group of render object intended to be rendered in one batch (i.e. because
 * requiring specific global render settings like enabled/disabled blending, depth test or write,
 * etc.)
 */
class IRenderLayerObject
{
public:
    virtual ~IRenderLayerObject() = default;

    /**
     * @brief Index of layer the object belongs to.
     *
     * @return Layer index, the lower is rendered first the higher later.
     */
    virtual RenderLayerId layer_index() const = 0;
};

}
