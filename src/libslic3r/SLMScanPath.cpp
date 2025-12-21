///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SLMScanPath.hpp"
#include "libslic3r/Point.hpp"
#include <cmath>

namespace Slic3r {

double ScanVector::length() const
{
    Point diff = end - start;
    return std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y()));
}

bool ScanVector::is_valid() const
{
    return start != end;
}

} // namespace Slic3r

