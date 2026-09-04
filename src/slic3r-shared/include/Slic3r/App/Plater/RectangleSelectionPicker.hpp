#pragma once

#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/App/Render/Material.hpp"

namespace Slic3r::App::Scene {
class Scene;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
struct Rect;
class ScreenInfo;
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater {

/**
 * @brief Picker for rectangle selection.
 *        This class implements selection of 3D volumes contained within a 2D screen space rectangle.
 *        It utilizes an offscreen framebuffer, having the size of the rectangle in screen space,
 *        where all the volumes in the scene intersecting the rectangle are rendered by assigning a unique color to each volume.
 *        The buffer is then parsed to extract a set of unique identifiers, which are then mapped back to the corresponding volume nodes.
 * @note  Rendering into an offscreen buffer allows to:
 *        1) correctly handle occlusions and overlapping volumes, so that the volume closest to the camera is always selected
 *        2) obtain pixel-accurate selections
 */
class RectangleSelectionPicker
{
public:
    explicit RectangleSelectionPicker(Render::Device& device);

    void pick(Scene::Scene& scene, const Render::ScreenInfo& screen_info, const Render::Rect& rect);

    const Scene::Node::NodeList& contained_nodes() const { return m_contained_nodes; }

private:
    void setup_scene(Scene::Scene& scene, const Render::Rect& rect);
    Domain::Image render(Scene::Scene& scene, const Render::Rect& rect);
    Scene::Node::NodeList collect_contained_nodes(const Domain::Image& image);
    void restore_scene(Scene::Scene& scene);

private:
    Render::Device& m_device;
    Render::Material m_material;

    Scene::Node::NodeList m_disabled_nodes;
    struct ModifiedNode
    {
        Scene::Node* node;
        std::optional<Render::Material> override_material;
        Render::Shadows shadows;
    };
    std::vector<ModifiedNode> m_modified_nodes;
    std::vector<ModifiedNode> m_bed_nodes;
    Scene::Node::NodeList m_contained_nodes;
};

} // namespace Slic3r::App::Plater
