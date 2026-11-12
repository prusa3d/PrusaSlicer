#include "Slic3r/Biz/Algorithms/ModelInstance.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::Biz::Algorithms::ModelInstance {

Domain::BoundingBox3d transformed_bounding_box(const Domain::BoundingBox3d& bbox, const Domain::ModelInstance& model_instance, const bool dont_translate)
{
    return BoundingBox::transformed(bbox, dont_translate ? model_instance.get_matrix_no_offset() : model_instance.get_matrix());
}

void transform_mesh(Domain::TriangleMesh& mesh, const Domain::ModelInstance& model_instance, const bool dont_translate)
{
    mesh.transform(dont_translate ? model_instance.get_matrix_no_offset() : model_instance.get_matrix());
}

Domain::Vec3d transformed_vector(const Domain::Vec3d& v, const Domain::ModelInstance& model_instance, const bool dont_translate)
{
    return dont_translate ? model_instance.get_matrix_no_offset() * v : model_instance.get_matrix() * v;
}

void transform_polygon(Domain::Polygon& polygon, const Domain::ModelInstance& model_instance)
{
    // CHECK_ME -> Is the following correct or it should take in account all three rotations?
    polygon.rotate(model_instance.get_rotation(Domain::Z)); // rotate around polygon origin
    // CHECK_ME -> Is the following correct?
    polygon.scale(model_instance.get_scaling_factor(Domain::X), model_instance.get_scaling_factor(Domain::Y)); // scale around polygon origin
}

} // namespace Slic3r::Biz::Algorithms::ModelInstance
