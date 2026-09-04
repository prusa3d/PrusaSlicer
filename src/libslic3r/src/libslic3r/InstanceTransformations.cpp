#include "libslic3r/InstanceTransformations.hpp"

namespace Slic3r {

InstanceTransformations transform_instances(Domain::Model& model, const Domain::Transform3d& transform)
{
    InstanceTransformations result;
    for (Domain::ModelObject* object : model.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            const Domain::Transformation& original_transformation{instance->get_transformation()};
            result[instance] = original_transformation;
            instance->set_transformation(
                Domain::Transformation{transform * original_transformation.get_matrix()}
            );
        }
    }
    return result;
}

void restore_instance_transformations(Domain::Model& model, const InstanceTransformations& original_transformations)
{
    for (auto& [instance, trafo] : original_transformations) {
        instance->set_transformation(trafo);
    }
}

} // namespace Slic3r
