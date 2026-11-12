#pragma once

#include <string>

#include "Slic3r/App/Scene/IRenderLayerObject.hpp"

namespace Slic3r::Domain {
struct BedInstance;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

class Node;
class NodeBuilder;
struct BedNodeTag;
class ScenePresenterProjectContext;

static constexpr double BED_OFFSET_Z = -0.025;

void build_bed_node(NodeBuilder& builder, const Domain::BedInstance& instance, const BedNodeTag& tag, Render::Device& device,
    ScenePresenterProjectContext& ctx, RenderLayerId layer_id);

/**
 * @brief Build a transient "virtual" bed preview subtree.
 *
 * Builds a full bed subtree with BedNodeTag::is_virtual set to true on all
 * nodes. BedRenderUpdater skips nodes with that flag, so build-time materials
 * and transforms are preserved.
 */
void build_virtual_bed_node(NodeBuilder& builder, const Domain::BedInstance& instance,
    std::size_t config_container_id, Render::Device& device,
    ScenePresenterProjectContext& ctx, RenderLayerId layer_id);

} // namespace Slic3r::App::Scene


