#pragma once

#include <Slic3r/Biz/libpgcode/Types.hpp>
#include <Slic3r/App/Render/ImguiTypes.hpp>
#include <Slic3r/Domain/Color.hpp>

#include <imgui.h>

namespace Slic3r::App::libvgcode {

static constexpr double INV255 = 1.0 / 255.0;

//
// Predefined values for the radius, in mm, of the cylinders used to render the travel moves.
//
static constexpr float DEFAULT_TRAVELS_RADIUS_MM = 0.1f;
static constexpr float MIN_TRAVELS_RADIUS_MM = 0.05f;
static constexpr float MAX_TRAVELS_RADIUS_MM = 1.0f;

//
// Predefined values for the radius, in mm, of the cylinders used to render the wipe moves.
//
static constexpr float DEFAULT_WIPES_RADIUS_MM = 0.1f;
static constexpr float MIN_WIPES_RADIUS_MM = 0.05f;
static constexpr float MAX_WIPES_RADIUS_MM = 1.0f;

//
// Predefined colors
//
static const Domain::ColorRGB DUMMY_COLOR = { 0.25f, 0.25f, 0.25f };

//
// Color palette
//
using Palette = std::vector<Domain::ColorRGB>;

//
// One dimensional natural numbers interval
// [0] -> min
// [1] -> max
//
using Interval = std::array<size_t, 2>;

//
// View types
//
enum class ViewType : uint8_t
{
    FeatureType,
    Height,
    Width,
    Speed,
    ActualSpeed,
    FanSpeed,
    Temperature,
    VolumetricFlowRate,
    ActualVolumetricFlowRate,
    LayerTimeLinear,
    LayerTimeLogarithmic,
    Tool,
    ColorPrint,
    COUNT
};

static constexpr size_t VIEW_TYPES_COUNT = size_t(ViewType::COUNT);

//
// Parameters for export to obj file
//
struct ObjExportParams
{
    std::string app_name;
    std::string app_version;
    std::string materials_filename;
    float cap_rounding_factor{ 0.25f };
};

struct ColorPrint
{
    uint8_t extruder_id{ 0 };
    uint8_t color_id{ 0 };
    uint32_t layer_id{ 0 };
    Biz::libpgcode::Times times{};
};

using ColorPrints = std::vector<ColorPrint>;

} // namespace Slic3r::App::libvgcode
