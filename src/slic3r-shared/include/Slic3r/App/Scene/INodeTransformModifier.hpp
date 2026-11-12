#pragma once
#include "Slic3r/App/Scene/Transform.hpp"

namespace Slic3r::App::Scene {

/**
 * @brief Interface providing ability to dynamically change node's world transform whenever the node local
 * transform changes.
 */
class INodeTransformModifier {
public:
    virtual ~INodeTransformModifier() = default;

    /**
     * @brief Modify computed world transform for node.
     * This function is called whenever node's world transform gets recomputed. The function implementation
     * should adjust the @p xform (like changing scale or rotation).
     * @param [in, out] xform World transform as computed from local transform and parent world transform.
     */
    virtual void modify_world_transform(Transform& xform) = 0;

};

}
