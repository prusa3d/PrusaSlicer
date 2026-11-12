#pragma once


#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Domain {
class Transformation;
} // namespace Slic3r::Domain

namespace cereal {
template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::Transformation& transformation);
} // namespace cereal

namespace Slic3r::Domain {

// Sets the given transform by assembling the given translation
void translation_transform(Domain::Transform3d& transform, const Domain::Vec3d& translation);

// Returns the transform obtained by assembling the given translation
Domain::Transform3d translation_transform(const Domain::Vec3d& translation);

// Returns the euler angles extracted from the given rotation matrix
// Warning -> The matrix should not contain any scale or shear !!!
Domain::Vec3d extract_rotation(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix);

// Returns the euler angles extracted from the given affine transform
// Warning -> The transform should not contain any shear !!!
Domain::Vec3d extract_rotation(const Domain::Transform3d& transform);

// Sets the given transform by assembling the given rotations in the following order:
// 1) rotate X
// 2) rotate Y
// 3) rotate Z
void rotation_transform(Domain::Transform3d& transform, const Domain::Vec3d& rotation);

// Returns the transform obtained by assembling the given rotations in the following order:
// 1) rotate X
// 2) rotate Y
// 3) rotate Z
Domain::Transform3d rotation_transform(const Domain::Vec3d& rotation);


// Sets the given transform by assembling the given scale factors
void scale_transform(Domain::Transform3d& transform, double scale);
void scale_transform(Domain::Transform3d& transform, const Domain::Vec3d& scale);

// Returns the transform obtained by assembling the given scale factors
Domain::Transform3d scale_transform(double scale);
Domain::Transform3d scale_transform(const Domain::Vec3d& scale);

class Transformation
{
    Domain::Transform3d m_matrix{Domain::Transform3d::Identity()};

public:
    Transformation() = default;
    explicit Transformation(const Domain::Transform3d& transform) : m_matrix(transform) {}

    Domain::Vec3d get_offset() const { return m_matrix.translation(); }
    double get_offset(Domain::Axis axis) const { return get_offset()[axis]; }

    Domain::Transform3d get_offset_matrix() const;

    void set_offset(const Domain::Vec3d& offset) { m_matrix.translation() = offset; }
    void set_offset(Domain::Axis axis, double offset) { m_matrix.translation()[axis] = offset; }

    Domain::Vec3d get_rotation() const;
    double get_rotation(Domain::Axis axis) const { return get_rotation()[axis]; }

    Domain::Transform3d get_rotation_matrix() const;

    void set_rotation(const Domain::Vec3d& rotation);
    void set_rotation(Domain::Axis axis, double rotation);

    Domain::Vec3d get_scaling_factor() const;
    double get_scaling_factor(Domain::Axis axis) const { return get_scaling_factor()[axis]; }

    Domain::Transform3d get_scaling_factor_matrix() const;

    bool is_scaling_uniform() const
    {
        const Domain::Vec3d scale = get_scaling_factor();
        return std::abs(scale.x() - scale.y()) < 1e-8 && std::abs(scale.x() - scale.z()) < 1e-8;
    }

    void set_scaling_factor(const Domain::Vec3d& scaling_factor);
    void set_scaling_factor(Domain::Axis axis, double scaling_factor);

    Domain::Vec3d get_mirror() const;
    double get_mirror(Domain::Axis axis) const { return get_mirror()[axis]; }

    Domain::Transform3d get_mirror_matrix() const;

    bool is_left_handed() const { return m_matrix.linear().determinant() < 0; }

    void set_mirror(const Domain::Vec3d& mirror);
    void set_mirror(Domain::Axis axis, double mirror);

    bool has_skew() const;

    void reset();
    void reset_offset() { set_offset(Domain::Vec3d::Zero()); }
    void reset_rotation();
    void reset_scaling_factor();
    void reset_mirror() { set_mirror(Domain::Vec3d::Ones()); }
    void reset_skew();

    const Domain::Transform3d& get_matrix() const { return m_matrix; }
    Domain::Transform3d get_matrix_no_offset() const;
    Domain::Transform3d get_matrix_no_scaling_factor() const;

    Domain::Transform3d get_matrix_with_applied_shrinkage_compensation(
        const Domain::Vec3d& shrinkage_compensation
    ) const;

    void set_matrix(const Domain::Transform3d& transform) { m_matrix = transform; }

    Transformation operator*(const Transformation& other) const;

private:

    template<class Archive>
    friend void cereal::serialize(Archive& ar, Transformation& transformation);
};

struct TransformationSVD
{
    Domain::SquareMatrix3d u{Domain::SquareMatrix3d::Identity()};
    Domain::SquareMatrix3d s{Domain::SquareMatrix3d::Identity()};
    Domain::SquareMatrix3d v{Domain::SquareMatrix3d::Identity()};

    bool mirror{false};
    bool scale{false};
    bool anisotropic_scale{false};
    bool rotation{false};
    bool rotation_90_degrees{false};
    bool skew{false};

    explicit TransformationSVD(const Transformation& trafo) : TransformationSVD(trafo.get_matrix())
    {}
    explicit TransformationSVD(const Domain::Transform3d& trafo);

    Eigen::DiagonalMatrix<double, 3, 3> mirror_matrix() const
    {
        return Eigen::DiagonalMatrix<double, 3, 3>(this->mirror ? -1. : 1., 1., 1.);
    }
};

}
