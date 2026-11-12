#pragma once

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Scene {
struct OrientedBoundingBox
{
    Domain::Vec3d center{Domain::Vec3d::Zero()};
    Domain::Vec3d dimensions{Domain::Vec3d::Zero()};
    Domain::SquareMatrix3d rotation{Domain::SquareMatrix3d::Identity()};
};
}

