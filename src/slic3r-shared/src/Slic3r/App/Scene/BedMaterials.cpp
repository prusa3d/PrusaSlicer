#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Scene/PrintVolumeData.hpp"
#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include "Slic3r/Assert.hpp"

#include <cfloat>

using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;

namespace Slic3r::App::Scene {

static const ColorRGBA PLATE_DEFAULT_REGULAR        = { 0.235f, 0.235f, 0.235f, 1.0f };
static const ColorRGBA PLATE_DEFAULT_DISABLED       = { 0.525f, 0.525f, 0.525f, 1.0f };
static const ColorRGBA PLATE_DEFAULT_ERROR          = { 0.850f, 0.000f, 0.000f, 1.0f };
static const ColorRGBA PLATE_DEFAULT_DISABLED_ERROR = { 1.000f, 0.000f, 0.000f, 1.0f };

static const ColorRGB PLATE_TEXTURED_DARK_REGULAR  = { 0.235f, 0.235f, 0.235f };
static const ColorRGB PLATE_TEXTURED_LIGHT_REGULAR = { 0.365f, 0.365f, 0.365f }; 
static const ColorRGB PLATE_TEXTURED_DARK_ERROR    = { 0.550f, 0.000f, 0.000f };
static const ColorRGB PLATE_TEXTURED_LIGHT_ERROR   = { 0.750f, 0.000f, 0.000f };

static const ColorRGBA GRID_REGULAR  = { 0.75f, 0.75f, 0.75f, 0.75f };
static const ColorRGBA GRID_DISABLED = { 0.65f, 0.65f, 0.65f, 0.75f };

static const ColorRGBA CONTOUR_REGULAR  = Domain::ColorRGBA::ORANGE();
static const ColorRGBA CONTOUR_DISABLED = { 0.75f, 0.75f, 0.75f, 1.0f };

static const ColorRGBA PRINT_VOLUME_REGULAR  = Domain::ColorRGBA::ORANGE();
static const ColorRGBA PRINT_VOLUME_DISABLED = { 0.75f, 0.75f, 0.75f, 1.0f };

static const ColorRGBA MODEL_REGULAR        = { 0.235f, 0.235f, 0.235f, 1.0f };
static const ColorRGBA MODEL_DISABLED       = { 0.525f, 0.525f, 0.525f, 1.0f };
static const ColorRGBA MODEL_ERROR          = { 0.550f, 0.000f, 0.000f, 1.0f };
static const ColorRGBA MODEL_DISABLED_ERROR = { 1.000f, 0.000f, 0.000f, 1.0f };

static const ColorRGB LABEL_REGULAR             = Domain::ColorRGB::ORANGE();
static const ColorRGB LABEL_DISABLED            = { 0.8f, 0.8f, 0.8f };
static const ColorRGB LABEL_SECONDARY_SELECTION = { 0.6f, 0.6f, 0.6f };

static const ColorRGBA CC_SELECTION_BORDER = Domain::ColorRGBA::ORANGE();

static const ColorRGBA X_AXIS = ColorRGBA::X();
static const ColorRGBA Y_AXIS = ColorRGBA::Y();
static const ColorRGBA Z_AXIS = ColorRGBA::Z();

Render::Material BedMaterials::plate_default_material(const Render::Device& device)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", PLATE_DEFAULT_REGULAR)
        .set_transparent(PLATE_DEFAULT_REGULAR.is_transparent());

    // disable in-shader print volume detection for bed plate
    PrintVolumeData print_volume;
    print_volume.type = Domain::BedType::Invalid;
    set_uniforms(print_volume, ret);
    return ret;
}

Render::Material BedMaterials::plate_textured_material(const Render::Device& device, const Domain::Bed& bed)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("printbed"))
        .set_texture(0, BedRenderHelper::texture(bed, device.context().texture_manager()))
        .set_uniform("back_color_dark", PLATE_TEXTURED_DARK_REGULAR)
        .set_uniform("back_color_light", PLATE_TEXTURED_LIGHT_REGULAR);
    return ret;
}

Render::Material BedMaterials::grid_material(const Render::Device& device)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("flat"))
        .set_uniform("uniform_color", GRID_REGULAR)
        .set_transparent(GRID_REGULAR.is_transparent());
    return ret;
}

Render::Material BedMaterials::contour_material(const Render::Device& device)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("flat"))
        .set_uniform("uniform_color", CONTOUR_REGULAR)
        .set_transparent(CONTOUR_REGULAR.is_transparent());
    return ret;
}

Render::Material BedMaterials::print_volume_material(const Render::Device& device)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("flat"))
        .set_uniform("uniform_color", PRINT_VOLUME_REGULAR)
        .set_transparent(PRINT_VOLUME_REGULAR.is_transparent());
    return ret;
}

Render::Material BedMaterials::model_material(const Render::Device& device)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", MODEL_REGULAR)
        .set_transparent(MODEL_REGULAR.is_transparent());

    // disable in-shader print volume detection for bed model
    PrintVolumeData print_volume;
    print_volume.type = Domain::BedType::Invalid;
    set_uniforms(print_volume, ret);
    return ret;
}

Render::Material BedMaterials::axis_material(const Render::Device& device, uint8_t axis)
{
    ColorRGBA color;
    switch (axis)
    {
    case 0: { color = X_AXIS; break; }
    case 1: { color = Y_AXIS; break; }
    case 2: { color = Z_AXIS; break; }
    default: {
        // unsupported axis
        PANIC("Unsupported axis");
    }
    }
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("gouraud_light"))
        .set_uniform("uniform_color", color)
        .set_transparent(color.is_transparent());

    // disable in-shader print volume detection for bed axis
    PrintVolumeData print_volume;
    print_volume.type = Domain::BedType::Invalid;
    set_uniforms(print_volume, ret);
    return ret;
}

Render::Material BedMaterials::label_material(const Render::Device& device, const std::string& label)
{
    Render::Material ret;
    ret
        .set_shader(device.context().shader_manager().shader("flat_texture"))
        .set_texture(0, BedRenderHelper::label_texture(label, device.context().texture_manager(), LABEL_REGULAR))
        .set_transparent(true);
    return ret;
}

Render::Material BedMaterials::cc_selection_border_material(const Render::Device& device)
{
    Render::Material material;

    material
        .set_shader(device.context().shader_manager().shader("flat"))
        .set_uniform("uniform_color", CC_SELECTION_BORDER);

    return material;
}

Render::Material BedMaterials::plate_default_transparent_material(const Render::Material& primary_material)
{
    ColorRGBA color = PLATE_DEFAULT_DISABLED;
    color.a(0.0f);

    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", color)
        .set_transparent(true);
    return ret;
}

Render::Material BedMaterials::plate_default_unselected_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", PLATE_DEFAULT_DISABLED)
        .set_transparent(PLATE_DEFAULT_DISABLED.is_transparent());
    return ret;
}

Render::Material BedMaterials::plate_default_error_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", PLATE_DEFAULT_ERROR)
        .set_transparent(PLATE_DEFAULT_ERROR.is_transparent());
    return ret;
}

Render::Material BedMaterials::plate_default_unselected_error_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", PLATE_DEFAULT_DISABLED_ERROR)
        .set_transparent(PLATE_DEFAULT_DISABLED_ERROR.is_transparent());
    return ret;
}

Render::Material BedMaterials::plate_textured_transparent_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret.set_transparent(true);
    return ret;
}

Render::Material BedMaterials::plate_textured_error_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("back_color_dark", PLATE_TEXTURED_DARK_ERROR)
        .set_uniform("back_color_light", PLATE_TEXTURED_LIGHT_ERROR);
    return ret;
}

Render::Material BedMaterials::grid_unselected_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", GRID_DISABLED)
        .set_transparent(GRID_DISABLED.is_transparent());
    return ret;
}

Render::Material BedMaterials::contour_unselected_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", CONTOUR_DISABLED)
        .set_transparent(CONTOUR_DISABLED.is_transparent());
    return ret;
}

Render::Material BedMaterials::print_volume_unselected_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", PRINT_VOLUME_DISABLED)
        .set_transparent(PRINT_VOLUME_DISABLED.is_transparent());
    return ret;
}

Render::Material BedMaterials::model_unselected_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", MODEL_DISABLED)
        .set_transparent(MODEL_DISABLED.is_transparent());
    return ret;
}

Render::Material BedMaterials::model_error_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", MODEL_ERROR)
        .set_transparent(MODEL_ERROR.is_transparent());
    return ret;
}

Render::Material BedMaterials::model_unselected_error_material(const Render::Material& primary_material)
{
    Render::Material ret = primary_material;
    ret
        .set_uniform("uniform_color", MODEL_DISABLED_ERROR)
        .set_transparent(MODEL_DISABLED_ERROR.is_transparent());
    return ret;
}

Render::Material BedMaterials::label_unselected_material(const Render::Material& primary_material, const Render::Device& device,
   const std::string& label)
{
    Render::Material ret = primary_material;
    ret.set_texture(0, BedRenderHelper::label_texture(label, device.context().texture_manager(), LABEL_DISABLED));
    return ret;
}

Render::Material BedMaterials::label_secondary_selection_material(const Render::Material& primary_material, const Render::Device& device,
    const std::string& label)
{
    Render::Material ret = primary_material;
    ret.set_texture(0, BedRenderHelper::label_texture(label, device.context().texture_manager(), LABEL_SECONDARY_SELECTION));
    return ret;
}

} // namespace Slic3r::App::Scene
