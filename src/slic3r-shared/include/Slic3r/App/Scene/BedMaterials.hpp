#pragma once

#include <string>
#include <cstdint>

namespace Slic3r::App::Render {
class Material;
class Device;
} // Slic3r::App::Render

namespace Slic3r::Domain {
class Bed;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {

struct BedMaterials
{
    // materials for selected state
    static Render::Material plate_default_material(const Render::Device& device);
    static Render::Material plate_textured_material(const Render::Device& device, const Domain::Bed& bed);
    static Render::Material grid_material(const Render::Device& device);
    static Render::Material contour_material(const Render::Device& device);
    static Render::Material print_volume_material(const Render::Device& device);
    static Render::Material model_material(const Render::Device& device);
    static Render::Material axis_material(const Render::Device& device, uint8_t axis);
    static Render::Material label_material(const Render::Device& device, const std::string& label);
    static Render::Material cc_selection_border_material(const Render::Device& device);

    // materials for other states
    static Render::Material plate_default_transparent_material(const Render::Material& primary_material);
    static Render::Material plate_default_unselected_material(const Render::Material& primary_material);
    static Render::Material plate_default_error_material(const Render::Material& primary_material);
    static Render::Material plate_default_unselected_error_material(const Render::Material& primary_material);
    static Render::Material plate_textured_transparent_material(const Render::Material& primary_material);
    static Render::Material plate_textured_error_material(const Render::Material& primary_material);
    static Render::Material grid_unselected_material(const Render::Material& primary_material);
    static Render::Material contour_unselected_material(const Render::Material& primary_material);
    static Render::Material print_volume_unselected_material(const Render::Material& primary_material);
    static Render::Material model_unselected_material(const Render::Material& primary_material);
    static Render::Material model_error_material(const Render::Material& primary_material);
    static Render::Material model_unselected_error_material(const Render::Material& primary_material);
    static Render::Material label_unselected_material(const Render::Material& primary_material, const Render::Device& device,
        const std::string& label);
    static Render::Material label_secondary_selection_material(const Render::Material& primary_material, const Render::Device& device,
        const std::string& label);
 };

} // namespace Slic3r::App::Scene
