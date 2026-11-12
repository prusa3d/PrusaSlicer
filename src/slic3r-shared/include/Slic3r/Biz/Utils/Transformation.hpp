#pragma once

#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz {
/**
 * @param xform The relative transformation matrix to check.
 * @return true if the transform contains any rotation component around
 * the X or Y axes, which would change the direction of the Z-axis or
 * any tranlation in z.
 */
bool changes_z_rotation_or_position(const Domain::SquareMatrix4d& xform);
} // namespace Slic3r::Biz
