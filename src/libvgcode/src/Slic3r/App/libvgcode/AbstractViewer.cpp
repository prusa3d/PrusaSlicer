#include "Slic3r/App/libvgcode/AbstractViewer.hpp"
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Scene/Scene.hpp>

namespace Slic3r::App::libvgcode {

void AbstractViewer::init(Render::Device& device, Scene::Scene& scene, Scene::GeometryDataFactory& data_factory)
{
    m_device       = &device;
    m_scene        = &scene;
    m_data_factory = &data_factory;
}

void AbstractViewer::reset()
{
    m_layers.reset();
    m_view_range.reset();
}

void AbstractViewer::set_layers_range(Interval::value_type min, Interval::value_type max)
{
    min = std::clamp<Interval::value_type>(min, 0, m_layers.count() - 1);
    max = std::clamp<Interval::value_type>(max, 0, m_layers.count() - 1);
    m_layers.set_view_range(min, max);
    // force immediate update of the full range
    update_view_full_range();
    m_view_range.set_visible(m_view_range.enabled());
}

void AbstractViewer::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    // force update of the full range, to avoid clamping the visible range with full old values
    // when calling m_view_range.set_visible()
    update_view_full_range();
    m_view_range.set_visible(min, max);
}

float AbstractViewer::encoded_color(const Domain::ColorRGB& color) {
    int r = int(color.r_uchar());
    int g = int(color.g_uchar());
    int b = int(color.b_uchar());
    int i_color = r << 16 | g << 8 | b;
    return float(i_color);
}

} // namespace Slic3r::App::libvgcode
