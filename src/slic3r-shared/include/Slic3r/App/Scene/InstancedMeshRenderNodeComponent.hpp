#pragma once

#include "Slic3r/App/Scene/MeshRenderNodeComponent.hpp"

namespace Slic3r::App::Scene {

class InstancedMeshRenderNodeComponent : public MeshRenderNodeComponent
{
public:
    InstancedMeshRenderNodeComponent() = default;
    InstancedMeshRenderNodeComponent(const InstancedMeshRenderNodeComponent&) = default;
    InstancedMeshRenderNodeComponent(InstancedMeshRenderNodeComponent&&) = default;

    explicit InstancedMeshRenderNodeComponent(
        const Render::Geometry* geometry,
        const Render::Material& material,
        size_t count = 0,
        size_t offset = 0
    ) : MeshRenderNodeComponent(geometry, material, count, offset)
    {}

    InstancedMeshRenderNodeComponent& operator=(const InstancedMeshRenderNodeComponent&) = default;
    InstancedMeshRenderNodeComponent& operator=(InstancedMeshRenderNodeComponent&&) = default;

    void render(const Node& node, const Camera& camera, const Render::Material& resolved_material, Render::CommandBuffer& cmd_buffer ) const override;

    size_t instances_count() const { return m_instances_count; }
    void set_instances_count(size_t count) { m_instances_count = count; }

private:
    size_t m_instances_count{ 0 };
};

} // namespace Slic3r::App::Scene
