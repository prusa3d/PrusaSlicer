///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberLayer.hpp"
#include "libslic3r/Geometry.hpp"
#include <cmath>

namespace Slic3r {

double FiberPath::length() const
{
    if (polyline.points.size() < 2)
        return 0.0;
    
    double total_length = 0.0;
    for (size_t i = 1; i < polyline.points.size(); ++i) {
        Point diff = polyline.points[i] - polyline.points[i-1];
        total_length += unscale<double>(std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y())));
    }
    return total_length;
}

bool FiberPath::is_closed() const
{
    if (polyline.points.size() < 2)
        return false;
    return polyline.points.front() == polyline.points.back();
}

double FiberLayer::total_fiber_length() const
{
    double total = 0.0;
    for (const FiberPath &path : fiber_paths) {
        total += path.length();
    }
    return total;
}

} // namespace Slic3r

