#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Domain {

inline constexpr size_t MAX_BLEND_COMPONENTS = 3;

struct VirtualExtruderComponent
{
    unsigned int extruder_id;
    double ratio;

    bool operator==(const VirtualExtruderComponent& other) const;
    bool operator!=(const VirtualExtruderComponent& other) const;
};

using VirtualExtruderComponents = std::vector<VirtualExtruderComponent>;

struct VirtualExtruderGradientStop
{
    unsigned int extruder_id;
    double position;

    bool operator==(const VirtualExtruderGradientStop& other) const;
    bool operator!=(const VirtualExtruderGradientStop& other) const;
};

using VirtualExtruderGradientStops = std::vector<VirtualExtruderGradientStop>;

struct VirtualExtruderGradient
{
    std::optional<double> z_min;
    std::optional<double> z_max;
    std::vector<VirtualExtruderGradientStop> stops;

    bool operator==(const VirtualExtruderGradient& other) const;
    bool operator!=(const VirtualExtruderGradient& other) const;
};

struct VirtualExtruder
{
    enum class Type
    {
        Blend,
        Gradient,
    };

    unsigned int id;
    std::optional<std::string> color;
    VirtualExtruderComponents components;
    std::optional<VirtualExtruderGradient> gradient;

    Type type() const;

    bool operator==(const VirtualExtruder& other) const;
    bool operator!=(const VirtualExtruder& other) const;
};

using VirtualExtruders = std::vector<VirtualExtruder>;

} // namespace Slic3r::Domain
