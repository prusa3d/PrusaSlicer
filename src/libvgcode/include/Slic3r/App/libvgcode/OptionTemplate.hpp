#pragma once

#include <cstdint>
#include <cstddef>

namespace Slic3r::App::Render {
class Device;
class Material;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class NodeBuilder;
class GeometryDataFactory;
class Scene;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::libvgcode {

class OptionTemplate
{
public:
    OptionTemplate() = default;
    ~OptionTemplate() = default;
    OptionTemplate(const OptionTemplate&) = delete;
    OptionTemplate(OptionTemplate&&) = delete;
    OptionTemplate& operator = (const OptionTemplate&) = delete;
    OptionTemplate& operator = (OptionTemplate&&) = delete;

    void init(Render::Device& device, Scene::NodeBuilder& builder, Scene::GeometryDataFactory& data_factory);
};

} // namespace Slic3r::App::libvgcode
