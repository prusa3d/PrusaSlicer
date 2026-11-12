#pragma once

#include "Slic3r/App/libvgcode/Types.hpp"

namespace Slic3r::App::libvgcode {

void add_vertex(const Domain::Vec3f& position, const Domain::Vec3f& normal, std::vector<float>& vertices);
void add_triangle(uint16_t v1, uint16_t v2, uint16_t v3, std::vector<uint16_t>& indices);

} // namespace Slic3r::App::libvgcode
