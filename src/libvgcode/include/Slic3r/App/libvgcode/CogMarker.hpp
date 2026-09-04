#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class NodeBuilder;
class GeometryDataFactory;
class Scene;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

class CogMarker
{
public:
    CogMarker() = default;
    ~CogMarker() = default;
    CogMarker(const CogMarker&) = delete;
    CogMarker(CogMarker&&) = delete;
    CogMarker& operator = (const CogMarker&) = delete;
    CogMarker& operator = (CogMarker&&) = delete;

    /**
     * @brief Initialize rendering geometry
     *
     * @param device The current device.
     * @param builder The node builder to which the geometry will be attached to.
     * @param data_factory The geometry factory.
     */
    void init(Render::Device& device, Scene::NodeBuilder& builder, Scene::GeometryDataFactory& data_factory);
    //
    // Update values used to calculate the center of gravity
    //
    void update(const Domain::Vec3f& position, float mass);
    //
    // Reset values used to calculate the center of gravity
    //
    void reset();
    //
    // Return the calculated center of gravity position
    //
    Domain::Vec3f position() const;
    //
    // Return the total mass.
    //
    float total_mass() const { return m_total_mass; }

    float scale_factor() const { return m_scale_factor; }
    void set_scale_factor(float factor) { m_scale_factor = std::max(factor, 0.001f); }

private:
    //
    // Values used to calculate the center of gravity
    //
    float m_total_mass{ 0.0f };
    Domain::Vec3f m_total_position{ Domain::Vec3f::Zero() };
    float m_scale_factor{ DefaultScaleFactor };
    static constexpr float DefaultScaleFactor = 2.0f;
};

} // namespace Slic3r::App::libvgcode
