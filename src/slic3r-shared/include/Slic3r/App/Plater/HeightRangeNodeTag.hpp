#pragma once

namespace Slic3r::App::Plater {

struct HeightRangeNodeTag
{};

struct HeightRangePlaneNodeTag : public HeightRangeNodeTag
{
    enum class PlaneType
    {
        Min,
        Max
    };

    PlaneType plane_type;

    explicit HeightRangePlaneNodeTag(PlaneType plane_type) :
        HeightRangeNodeTag(),
        plane_type(plane_type)
    {}
};

} // namespace Slic3r::App::Plater
