#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Arrange {
Domain::Polygon to_rectangle(const Domain::BoundingBox2crd& bb);
} // namespace Slic3r::Biz::Arrange
