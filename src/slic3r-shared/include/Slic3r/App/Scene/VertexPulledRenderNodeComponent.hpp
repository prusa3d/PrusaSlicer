#pragma once

#include "Slic3r/App/Scene/IRenderNodeComponent.hpp"
#include "Slic3r/App/Render/Geometry.hpp"

namespace Slic3r::App::Scene {

class VertexPulledRenderNodeComponent : public IRenderNodeComponent
{
public:
    explicit VertexPulledRenderNodeComponent(
        Render::Device& device,
        const Render::DrawCommand& draw_command,
        const Render::Material& material
    ) :
        m_pull_geometry{device},
        m_material{material},
        m_draw_command{draw_command}
    {}

    const Render::Material& material() const override
    {
        return m_material;
    }

    void replace_material(const Render::Material& material) override
    {
        m_material = material;
    }

    void render(
        const Node& node,
        const Camera& camera,
        const Render::Material& resolved_material,
        Render::CommandBuffer& cmd_buffer
    ) const override;

    RenderLayerId layer_index() const override
    {
        return m_layer;
    }

    void set_layer_index(RenderLayerId layer)
    {
        m_layer = layer;
    }

    void set_geometry(const Render::Geometry* geometry, size_t count = 0, size_t offset = 0);

    void set_shadows(const Render::Shadows& shadows) override
    {
        m_shadows = shadows;
    }

    bool cast_shadows() const override
    {
        return m_shadows.cast;
    }

    bool receive_shadows() const override
    {
        return m_shadows.receive;
    }

    bool has_pbr() const override
    {
        return m_pbr.has_value();
    }

    void set_pbr(const PBRParams& pbr) override
    {
        m_pbr = pbr;
    }

    const std::optional<PBRParams>& pbr() const override
    {
        return m_pbr;
    }

    void set_print_volume(const PrintVolumeData& print_volume) override
    {
        m_print_volume = print_volume;
    }

    bool has_print_volume() const override
    {
        return m_print_volume.has_value();
    }

    const std::optional<PrintVolumeData>& print_volume() const override
    {
        return m_print_volume;
    }

    void set_total_vertex_count(std::size_t);

private:
    Render::PullGeometry m_pull_geometry;
    Render::Material m_material;
    Render::Shadows m_shadows;
    std::optional<PBRParams> m_pbr;
    std::optional<PrintVolumeData> m_print_volume;
    RenderLayerId m_layer{0};
    Render::DrawCommand m_draw_command;
};

} // namespace Slic3r::App::Scene
