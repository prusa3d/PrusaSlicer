#pragma once

#include <Slic3r/App/Render/Geometry.hpp>

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class NodeBuilder;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

void init_segments_node(Render::Device& device, Scene::NodeBuilder& builder);

} // namespace Slic3r::App::libvgcode
