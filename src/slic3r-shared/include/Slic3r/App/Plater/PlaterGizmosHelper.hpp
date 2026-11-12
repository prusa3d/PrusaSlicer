#pragma once

#include "Slic3r/Domain/Types.hpp"


namespace Slic3r::App::Scene {
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz::Scene {
struct ObjectSelection;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {

/**
 * @brief Returns true if the given node can be added to the given object selection.
 * @param node The node to check.
 * @param selection The object selection candidate to host the node.
 * @return true if the given node can be added to the given object selection, false otherwise.
 */
bool can_be_added_to_object_selection(const Scene::Node& node, const Biz::Scene::ObjectSelection& selection);

Domain::SquareMatrix4d get_scale_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& center,
    const Domain::Vec3d& scale_by
);

Domain::SquareMatrix3d get_local_rotation_matrix(const Domain::Vec3d& rotate_by);

Domain::SquareMatrix4d get_rotation_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& center,
    const Domain::Vec3d& rotate_by
);

Domain::SquareMatrix4d get_translation_matrix(
    const Domain::SquareMatrix3d& basis,
    const Domain::Vec3d& translate_by
);
} // namespace Slic3r::App::Plater
