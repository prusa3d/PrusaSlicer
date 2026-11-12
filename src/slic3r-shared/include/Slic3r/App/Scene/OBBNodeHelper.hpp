#pragma once

#include "Slic3r/App/Scene/IRenderLayerObject.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Biz/Scene/OrientedBoundingBox.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"

#include <string>
#include <optional>

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

class NodeBuilder;
class Node;

/**
  * @brief Node tag for aabb
  */
struct AABBNodeTag
{
    uint8_t corner_id{ 0 };
};

void build_obb_node(NodeBuilder& builder, Render::GeometryManager<AuxiliaryElementId>& geom_manager, Render::Device& device,
    const std::string& debug_name, RenderLayerId layer_id, const Domain::ColorRGB& color = Domain::ColorRGB::WHITE());

void update_obb_node(Node& node, const Biz::Scene::OrientedBoundingBox& obb, double edge_coverage_percent = 1.0,
    std::optional<Domain::ColorRGB> color = std::nullopt);

} // namespace Slic3r::App::Scene
