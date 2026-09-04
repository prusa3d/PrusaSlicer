#pragma once

#include <optional>

#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::CGAL::Algorithms {

/**
 * @brief Merge the object volumes into a single mesh in object coordinates, with model
 *        parts united and negative volumes subtracted.
 *
 * @return std::nullopt when the booleans could not be performed on the input meshes.
 */
std::optional<Domain::TriangleMesh> merge_object_volumes(const Domain::ModelObject& model_object);

} // namespace Slic3r::Biz::CGAL::Algorithms
