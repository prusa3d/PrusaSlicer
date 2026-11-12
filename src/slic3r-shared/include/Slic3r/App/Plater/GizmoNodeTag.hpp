#pragma once

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <cstdint>
#include <optional>

namespace Slic3r::App::Plater {

enum class AxisType : uint8_t
{
    None = 0,
    XAxis = 1,
    YAxis = 2,
    ZAxis = 3,
    XYAxis = 4
};

inline std::optional<int> get_axis_index(AxisType axis)
{
    switch (axis) {
    case AxisType::XAxis:
        return 0;
        break;
    case AxisType::YAxis:
        return 1;
        break;
    case AxisType::ZAxis:
        return 2;
        break;
    default:
        return std::nullopt;
        break;
    }
}

Domain::Vec3d axis_type_dir(AxisType at);

struct TranslationGizmoNodeTag
{
    const AxisType primary_axis;

    explicit TranslationGizmoNodeTag(AxisType primary_axis) : primary_axis(primary_axis) {}

    Domain::Vec3d primary_axis_dir() const
    {
        return axis_type_dir(primary_axis);
    }
};

struct RotationGizmoNodeTag
{
    AxisType primary_axis;
    bool is_handle{false};

    explicit RotationGizmoNodeTag(AxisType primary_axis, bool is_handle = false) :
        primary_axis(primary_axis),
        is_handle(is_handle)
    {}

    Domain::Vec3d primary_axis_dir() const
    {
        return axis_type_dir(primary_axis);
    }
};

enum class DragCube {
    None,
    Left,
    Right,
    Front,
    Back,
    Bottom,
    Top,
    FrontLeft,
    FrontRight,
    BackLeft,
    BackRight,
};

struct ScaleGizmoNodeTag
{
    AxisType axis{AxisType::None};
    DragCube drag_cube{DragCube::None};
};


inline Domain::Vec3d axis_type_dir(AxisType at)
{
    Domain::Vec3d ret = Domain::Vec3d::Zero();
    if (at != AxisType::None)
        ret[int(at) - 1] = 1;
    return ret;
}

inline Domain::ColorRGBA axis_color(AxisType axis)
{
    switch (axis)
    {
    case AxisType::XAxis: { return Domain::ColorRGBA::X(); }
    case AxisType::YAxis: { return Domain::ColorRGBA::Y(); }
    case AxisType::ZAxis: { return Domain::ColorRGBA::Z(); }
    default:              { return Domain::ColorRGBA::BLACK(); }
    }
}

inline std::string axis_string(AxisType axis)
{
    switch (axis)
    {
    case AxisType::XAxis: { return "X axis"; }
    case AxisType::YAxis: { return "Y axis"; }
    case AxisType::ZAxis: { return "Z axis"; }
    default:              { return "?"; }
    }
}

}
