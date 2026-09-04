#include "Slic3r/App/libvgcode/SegmentTemplate.hpp"

#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include "Slic3r/App/libvgcode/GCodeNodeTag.hpp"
#include <Slic3r/App/Preview/PreviewSceneLayer.hpp>
#include <Slic3r/App/Scene/NodeBuilder.hpp>
#include <Slic3r/App/Scene/Scene.hpp>

namespace Slic3r::App::libvgcode {

void init_segments_node(Render::Device& device, Scene::NodeBuilder& builder)
{
    const Render::Material material{
        Render::Material{}.set_shader(device.context().shader_manager().shader("segments"))
    };

    const Render::DrawCommand draw_command{Render::PrimitiveType::Triangles, 0, 0, material};

    builder.set_debug_name("gcode_toolpaths")
        .set_tag(GCodeNodeTag{GCodeElementType::Toolpaths})
        .set_vertex_pulling(
            device,
            draw_command,
            material,
            Scene::RenderLayerId(Preview::PreviewSceneLayer::Toolpaths)
        )
        .set_shadows(Render::Shadows{true, true})
        .set_pbr(Scene::DEFAULT_GCODE_SEGMENTS_PBRPARAMS);
}

} // namespace Slic3r::App::libvgcode
