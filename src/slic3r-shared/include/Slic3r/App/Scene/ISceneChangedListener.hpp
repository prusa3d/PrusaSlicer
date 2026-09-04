#pragma once

namespace Slic3r::App::Scene {

class Node;

class ISceneChangedListener
{
public:
    virtual ~ISceneChangedListener() = default;

    /**
     * @brief Called whenever a node is going to be added to the scene.
     * @param node Pointer to the node that is going to be added
     */
    virtual void on_node_added(Node* node) = 0;

    /**
     * @brief Called whenever a node is going to be removed from the scene.
     * @param node Pointer to the node that is going to be removed
     */
    virtual void on_node_removed(Node* node) = 0;

    /**
     * @brief Called whenever a node state is modified.
     * @param node Pointer to the modified node
     */
    virtual void on_node_changed(Node* node) {}
};

} // namespace Slic3r::App::Scene
