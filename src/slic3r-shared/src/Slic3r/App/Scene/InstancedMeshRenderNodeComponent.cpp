#include "Slic3r/App/Scene/InstancedMeshRenderNodeComponent.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::SquareMatrix3f;
using Slic3r::Domain::SquareMatrix4f;

namespace Slic3r::App::Scene {

const std::string UNIFORM_MODEL_MATRIX                 = "model_matrix";
const std::string UNIFORM_VIEW_MODEL_MATRIX            = "view_model_matrix";
const std::string UNIFORM_VIEW_MATRIX                  = "view_matrix";
const std::string UNIFORM_PROJECTION_MATRIX            = "projection_matrix";
const std::string UNIFORM_PROJECTION_VIEW_MODEL_MATRIX = "projection_view_model_matrix";
const std::string UNIFORM_VIEW_NORMAL_MATRIX           = "view_normal_matrix";

void InstancedMeshRenderNodeComponent::render(const Node& node, const Camera& camera, const Render::Material& resolved_material,
    Render::CommandBuffer& cmd_buffer) const
{
    if (m_instances_count == 0)
        return;

    Render::Material material = resolved_material;

    // Set transform uniforms
    SquareMatrix4f view = camera.view().matrix().cast<float>();
    SquareMatrix4f model = node.world_transform().matrix().cast<float>();
    SquareMatrix4f proj = camera.projection().cast<float>();
    SquareMatrix4f model_view = view * model;
    SquareMatrix4f pvm = proj * model_view;
    SquareMatrix3f normal = (view.block<3, 3>(0, 0) * model.block<3, 3>(0, 0)).inverse().transpose();

    // update per-node uniforms
    material.set_uniform(UNIFORM_MODEL_MATRIX, model);
    material.set_uniform(UNIFORM_VIEW_MATRIX, view);
    material.set_uniform(UNIFORM_VIEW_MODEL_MATRIX, model_view);
    material.set_uniform(UNIFORM_PROJECTION_MATRIX, proj);
    material.set_uniform(UNIFORM_PROJECTION_VIEW_MODEL_MATRIX, pvm);
    material.set_uniform(UNIFORM_VIEW_NORMAL_MATRIX, normal);

    cmd_buffer.bind_and_draw_instanced(*m_geometry, material, m_instances_count);
}

} // namespace Slic3r::App::Scene
