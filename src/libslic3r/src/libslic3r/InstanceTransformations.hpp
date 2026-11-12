#pragma once

#include "Slic3r/Domain/Model.hpp"

namespace Slic3r {

using InstanceTransformations = std::map<Domain::ModelInstance*, Domain::Transformation>;

// Returns original transformations.
InstanceTransformations transform_instances(Domain::Model& model, const Domain::Transform3d& transform);
void restore_instance_transformations(Domain::Model& model, const InstanceTransformations& original_transformations);

} // namespace Slic3r
