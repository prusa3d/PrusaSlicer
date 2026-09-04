#pragma once
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Axis.hpp"

namespace Slic3r::Domain {

class ITransformable
{
public:
    virtual ~ITransformable() = default;

    virtual const Transformation& get_transformation() const              = 0;
    virtual void set_transformation(const Transformation& transformation) = 0;
    virtual Vec3d get_offset() const                                      = 0;
    virtual double get_offset(Axis axis) const                            = 0;
    virtual void set_offset(const Vec3d& offset)                          = 0;
    virtual void set_offset(Axis axis, double offset)                     = 0;
    virtual Vec3d get_rotation() const                                    = 0;
    virtual double get_rotation(Axis axis) const                          = 0;
    virtual void set_rotation(const Vec3d& rotation)                      = 0;
    virtual void set_rotation(Axis axis, double rotation)                 = 0;
    virtual Vec3d get_scaling_factor() const                              = 0;
    virtual double get_scaling_factor(Axis axis) const                    = 0;
    virtual void set_scaling_factor(const Vec3d& scaling_factor)          = 0;
    virtual void set_scaling_factor(Axis axis, double scaling_factor)     = 0;
    virtual Vec3d get_mirror() const                                      = 0;
    virtual double get_mirror(Axis axis) const                            = 0;
    virtual void set_mirror(const Vec3d& mirror)                          = 0;
    virtual void set_mirror(Axis axis, double mirror)                     = 0;
    virtual const Transform3d& get_matrix() const                         = 0;
    virtual Transform3d get_matrix_no_offset() const                      = 0;
    virtual bool is_left_handed() const                                   = 0;
};

} // namespace Slic3r::Domain
