#pragma once

#include <vector>
#include <functional>

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App::Plater {
class RibbonMeshGenerator
{
public:
    using Points = std::vector<Domain::Vec3f>;
    using EmitFn = std::function<void(const Domain::Vec3f& v)>;

    explicit RibbonMeshGenerator(
        EmitFn emit,
        float width         = 1.0f,
        float corner_radius = 0.5f,
        int circle_segments = 36
    ) :
        emit(std::move(emit)),
        width(width),
        corner_radius(corner_radius),
        circle_segments(circle_segments)
    {}

    void generate(const Points& points, bool closed) const;

public:
    EmitFn emit;
    float width;
    float corner_radius;
    int circle_segments; // Number of segments for a full 360-degree circle
};

} // namespace Slic3r::App::Plater
