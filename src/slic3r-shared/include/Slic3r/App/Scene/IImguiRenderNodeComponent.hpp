#pragma once

#include <functional>

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::App::Scene {

/**
 * @brief 2D GUI overlay rendering interface
 *
 * Defines GUI rendering interface for a Node. Requires IRaycastNodeComponent to be placed
 * to same node or any of its parents. This is used to get projected Aabb2f to pin the GUI
 * overlay to.
 */
class IImguiRenderNodeComponent
{
public:
    using Aabb2f = Eigen::AlignedBox<float, 2>;

    virtual ~IImguiRenderNodeComponent() = default;

    /**
     * @brief Render GUI overlay for given node and its related screen projection of bounding box.
     * @param node Node the ImGui render component is attached to.
     * @param screen_bounding_box Resolved bounding box projected to ImGui screen coordinates.
     */
    virtual void render_imgui(const Node& node, const Aabb2f& screen_bounding_box) const = 0;
};

/**
 * @brief Functional adapter for IImguiRenderNodeComponent
 */
class FuncImguiRenderNodeComponent : public IImguiRenderNodeComponent
{
public:
    using RenderFunc = std::function<void(const Node&, const Aabb2f&)>;

    explicit FuncImguiRenderNodeComponent(const RenderFunc render_func) : m_render_func(render_func)
    {}

    void render_imgui(const Node& node, const Aabb2f& screen_bounding_box) const override
    {
        m_render_func(node, screen_bounding_box);
    }

private:
    RenderFunc m_render_func;
};

}
