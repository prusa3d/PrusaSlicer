#include "Slic3r/App/Scene/PrintVolumeData.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/BedNodeBuilder.hpp"

namespace Slic3r::App::Scene {

void set_uniforms(const std::optional<PrintVolumeData>& volume_data, Render::Material& material)
{
    if (volume_data.has_value()) {
        material
            .set_uniform("print_volume.type", int(volume_data->type))
            .set_uniform("print_volume.z_data", volume_data->z_data)
            .set_uniform("print_volume.xy_data", volume_data->xy_data);
    }
    else {
        material
            .set_uniform("print_volume.type", 0)
            .set_uniform("print_volume.z_data", Domain::Vec2f{ float(BED_OFFSET_Z), FLT_MAX })
            .set_uniform("print_volume.xy_data", Domain::Vec4f{ -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX });
    }
}

} // namespace Slic3r::App::Scene
