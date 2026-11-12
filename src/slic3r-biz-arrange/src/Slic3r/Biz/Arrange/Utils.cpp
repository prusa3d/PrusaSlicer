#include "Slic3r/Biz/Arrange/Utils.hpp"

namespace Slic3r::Biz::Arrange {

using Domain::BoundingBox2crd;
using Domain::Polygon;
using Domain::Vec2crd;

Polygon to_rectangle(const BoundingBox2crd& bb)
{
    Polygon ret;
    ret.points = {bb.min, Vec2crd{bb.max.x(), bb.min.y()}, bb.max, Vec2crd{bb.min.x(), bb.max.y()}};

    return ret;
}
} // namespace Slic3r::Biz::Arrange
