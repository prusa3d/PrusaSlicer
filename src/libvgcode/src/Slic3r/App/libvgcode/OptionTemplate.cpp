#include "Slic3r/App/libvgcode/OptionTemplate.hpp"
#include "Slic3r/App/libvgcode/Utils.hpp"

#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include "Slic3r/App/libvgcode/GCodeNodeTag.hpp"
#include <Slic3r/App/Preview/PreviewSceneLayer.hpp>
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include <Slic3r/App/Scene/Scene.hpp>

#include <cmath>

namespace Slic3r::App::libvgcode {

// Geometry:
// diamond with 'resolution' sides, centered at (0.0, 0.0, 0.0)
// height and width of the diamond are equal to 1.0
void OptionTemplate::init(Render::Device& device, Scene::NodeBuilder& builder, Scene::GeometryDataFactory& data_factory)
{
    Render::Material material = Render::Material{}
        .set_shader(device.context().shader_manager().shader("options"));

    builder
        .set_debug_name("gcode_options")
        .set_tag(GCodeNodeTag{ GCodeElementType::Options })
        .set_mesh_instanced(data_factory.geometry(Scene::GeometryDataId::CandyButton), material, 0,
            Scene::RenderLayerId(Preview::PreviewSceneLayer::Options))
        .set_shadows(Render::Shadows{ true, true })
        .set_pbr(Scene::DEFAULT_GCODE_OPTIONS_PBRPARAMS);
}

} // namespace Slic3r::App::libvgcode
