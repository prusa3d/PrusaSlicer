#pragma once
#include "Slic3r/App/Scene/INodeTransformModifier.hpp"
#include "Slic3r/App/Scene/Camera.hpp"

namespace Slic3r::App::Scene {
class Node;

constexpr double SELECTION_ROOT_SCALE_MODIFIER{0.0075};

/**
 * @brief Modify node's world scale so it is constant in screenspace i.e. the size of node is same
 * independently of camera zoom/distance.
 *
 * This may be handy for gizmos.
 */
class ScreenSpaceSizedTransformModifier : public INodeTransformModifier, public ICameraUpdateListener {
public:
    ScreenSpaceSizedTransformModifier(const Camera& cam, Node& node, double scale=1)
        : m_camera(cam), m_node(node), m_preserved_scale(scale)
    {}

    void modify_world_transform(Transform& world_xform) override;
    void camera_updated(const Camera& cam) override;

private:
    const Camera& m_camera;
    Node& m_node;
    double m_preserved_scale;
};
}
