#include "Slic3r/App/libvgcode/Utils.hpp"

#include <cmath>

namespace Slic3r::Biz::libvgcode {

using Domain::Vec3f;

void add_vertex(const Vec3f& position, const Vec3f& normal, std::vector<float>& vertices)
{
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(normal.x());
    vertices.emplace_back(normal.y());
    vertices.emplace_back(normal.z());
}

void add_triangle(uint16_t v1, uint16_t v2, uint16_t v3, std::vector<uint16_t>& indices)
{
    indices.emplace_back(v1);
    indices.emplace_back(v2);
    indices.emplace_back(v3);
}

} // namespace Slic3r::Biz::libvgcode

