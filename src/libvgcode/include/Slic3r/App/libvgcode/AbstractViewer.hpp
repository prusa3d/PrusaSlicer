#pragma once

#include "Types.hpp"
#include "ViewRange.hpp"
#include "Layers.hpp"

#include <Slic3r/App/Scene/Lights.hpp>

namespace Slic3r::Domain {
class ColorRGB;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class Scene;
class GeometryDataFactory;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

class AbstractViewer
{
public:
    AbstractViewer() = default;
    ~AbstractViewer() = default;
    AbstractViewer(const AbstractViewer& other) = delete;
    AbstractViewer(AbstractViewer&& other) = delete;
    AbstractViewer& operator = (const AbstractViewer& other) = delete;
    AbstractViewer& operator = (AbstractViewer&& other) = delete;

    /**
     * @brief Initialize the viewer
     *
     * @param device The current device.
     * @param scene The current scene.
     * @param data_factory The geometry factory.
     */
    virtual void init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory);

    /**
     * @brief Set the current scene for the viewer.
     *
     * @param scene The scene to set.
     */
    virtual void set_scene(Scene::Scene& scene)
    {
        m_scene = &scene;
    }

    virtual void clear_scene() = 0;

    //
    // Reset the contents of the viewer.
    // Automatically called by load() method.
    //
    virtual void reset();

    //
    // Render according to the current settings
    //
    virtual void render() = 0;

    size_t layers_count() const { return m_layers.count(); }
    float layer_z(size_t layer_id) const { return m_layers.layer_z(layer_id); }
    std::vector<float> layers_zs() const { return m_layers.zs(); }

    size_t layer_id_at(float z) const { return m_layers.layer_id_at(z); }
    virtual float estimated_time() const = 0;
    virtual float estimated_time_at(size_t id) const = 0;
    virtual std::vector<float> layers_estimated_times() const = 0;

    const Interval& layers_range() const { return m_layers.view_range(); }
    void set_layers_range(const Interval& range) { set_layers_range(range[0], range[1]); }
    virtual void set_layers_range(Interval::value_type min, Interval::value_type max);

    const Interval& view_full_range() const { return m_view_range.full(); }
    const Interval& view_enabled_range() const { return m_view_range.enabled(); }
    const Interval& view_visible_range() const { return m_view_range.visible(); }
    virtual void set_view_visible_range(Interval::value_type min, Interval::value_type max);

protected:
    virtual void update_view_full_range() = 0;
    float encoded_color(const Domain::ColorRGB& color);

protected:
    //
    // Detected layers
    //
    Layers m_layers;
    //
    // Vertices ranges for visualization
    //
    ViewRange m_view_range;

    Render::Device* m_device{nullptr};
    Scene::Scene* m_scene{nullptr};
    Scene::GeometryDataFactory* m_data_factory{nullptr};
};

} //namespace Slic3r::App::libvgcode