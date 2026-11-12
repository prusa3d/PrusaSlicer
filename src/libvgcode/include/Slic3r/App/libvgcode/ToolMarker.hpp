#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"
#include "Slic3r/Domain/Color.hpp"

#include "Slic3r/Domain/BoundingBox.hpp"

#include <cfloat>

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class NodeBuilder;
class GeometryDataFactory;
class Scene;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

class ToolMarker
{
public:
    ToolMarker() = default;
    ~ToolMarker() = default;
    ToolMarker(const ToolMarker&) = delete;
    ToolMarker(ToolMarker&&) = delete;
    ToolMarker& operator = (const ToolMarker&) = delete;
    ToolMarker& operator = (ToolMarker&&) = delete;

    /**
     * @brief Initialize rendering geometry
     *
     * @param device The current device.
     * @param builder The node builder to which the geometry will be attached to.
     * @param data_factory The geometry factory.
     */
    void init(Render::Device& device, Scene::NodeBuilder& builder, Scene::GeometryDataFactory& data_factory);

    bool enabled() const { return m_enabled; }
    void set_enabled(bool enabled) { m_enabled = enabled; }

    float offset_z() const { return m_offset_z; }
    void set_offset_z(float offset_z) { m_offset_z = std::max(offset_z, 0.0f); }

    const Domain::ColorRGB& color() const { return m_color; }
    void set_color(const Domain::ColorRGB& color) { m_color = color; }

    float alpha() const { return m_alpha; }
    void set_alpha(float alpha) { m_alpha = std::clamp(alpha, 0.25f, 0.75f); }

    float scale_factor() const { return m_scale_factor; }
    void set_scale_factor(float factor) { m_scale_factor = std::max(factor, 0.001f); }

    const Domain::BoundingBox3d& bounding_box() const { return m_bounding_box; }

private:
    bool m_enabled{ true };
    float m_offset_z{ 0.5f };
    Domain::ColorRGB m_color{ Domain::ColorRGB::WHITE() };
    float m_alpha{ 0.5f };
    Domain::BoundingBox3d m_bounding_box;
    float m_scale_factor{ DefaultScaleFactor };
    static constexpr float DefaultScaleFactor = 1.0f;
};

} // namespace Slic3r::App::libvgcode
