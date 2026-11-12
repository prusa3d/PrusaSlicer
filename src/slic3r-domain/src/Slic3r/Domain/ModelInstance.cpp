#include "Slic3r/Domain/ModelInstance.hpp"

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/BedInstance.hpp"

namespace Slic3r::Domain {

Domain::ModelObject* ModelInstance::get_object() const { return this->object; }

void ModelInstance::set_model_object(ModelObject* model_object) { this->object = model_object; }

const BedRef& ModelInstance::get_last_bed() const
{
    return m_last_bed;
}

void ModelInstance::set_last_bed(const BedRef& last_bed)
{
    m_last_bed = last_bed;
}

const Domain::Transformation& ModelInstance::get_transformation() const { return m_transformation; }

void ModelInstance::set_transformation(const Domain::Transformation& transformation)
{
    m_transformation = transformation;
    this->object->invalidate_bounding_box();
}

Vec3d ModelInstance::get_offset() const { return m_transformation.get_offset(); }

double ModelInstance::get_offset(Axis axis) const { return m_transformation.get_offset(axis); }

void ModelInstance::set_offset(const Vec3d& offset) { m_transformation.set_offset(offset); }

void ModelInstance::set_offset(Axis axis, double offset) { m_transformation.set_offset(axis, offset); }

Vec3d ModelInstance::get_rotation() const { return m_transformation.get_rotation(); }

double ModelInstance::get_rotation(Axis axis) const { return m_transformation.get_rotation(axis); }

void ModelInstance::set_rotation(const Vec3d& rotation) { m_transformation.set_rotation(rotation); }

void ModelInstance::set_rotation(Axis axis, double rotation) { m_transformation.set_rotation(axis, rotation); }

Vec3d ModelInstance::get_scaling_factor() const { return m_transformation.get_scaling_factor(); }

double ModelInstance::get_scaling_factor(Axis axis) const { return m_transformation.get_scaling_factor(axis); }

void ModelInstance::set_scaling_factor(const Vec3d& scaling_factor) { m_transformation.set_scaling_factor(scaling_factor); }

void ModelInstance::set_scaling_factor(Axis axis, double scaling_factor) { m_transformation.set_scaling_factor(axis, scaling_factor); }

Vec3d ModelInstance::get_mirror() const { return m_transformation.get_mirror(); }

double ModelInstance::get_mirror(Axis axis) const { return m_transformation.get_mirror(axis); }

void ModelInstance::set_mirror(const Vec3d& mirror) { m_transformation.set_mirror(mirror); }

void ModelInstance::set_mirror(Axis axis, double mirror) { m_transformation.set_mirror(axis, mirror); }

const Transform3d& ModelInstance::get_matrix() const { return m_transformation.get_matrix(); }

Transform3d ModelInstance::get_matrix_no_offset() const { return m_transformation.get_matrix_no_offset(); }

bool ModelInstance::is_left_handed() const { return m_transformation.is_left_handed(); }

bool ModelInstance::is_printable() const
{
    return this->printable && (this->print_volume_state == ModelInstancePVS_Inside);
}

bool ModelInstance::operator==(const ModelInstance& rhs) const
{
    if (this->object != rhs.object) {
        return false;
    }

    if (!this->m_transformation.get_matrix().isApprox(rhs.m_transformation.get_matrix())) {
        return false;
    }

    if (this->print_volume_state != rhs.print_volume_state) {
        return false;
    }

    if (this->printable != rhs.printable) {
        return false;
    }

    return true;
}

} // namespace Slic3r::Domain
