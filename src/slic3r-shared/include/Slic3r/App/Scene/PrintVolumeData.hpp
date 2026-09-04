#pragma once

#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/Point.hpp"

#include <optional>
#include <cfloat>

namespace Slic3r::App::Render {
class Material;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {

struct PrintVolumeData
{
    Domain::BedType type{ Domain::BedType::Rectangle };
    Domain::Vec4f xy_data{ -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX };
    Domain::Vec2f z_data{ -FLT_MAX, FLT_MAX };
};

void set_uniforms(const std::optional<PrintVolumeData>& volume_data, Render::Material& material);

} // namespace Slic3r::App::Scene
