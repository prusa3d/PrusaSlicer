#include <numbers>

#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Math.hpp"


namespace Slic3r::Domain {

void translation_transform(Transform3d& transform, const Vec3d& translation)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
}

Transform3d translation_transform(const Vec3d& translation)
{
    Transform3d transform;
    translation_transform(transform, translation);
    return transform;
}

Transform3d Transformation::get_offset_matrix() const
{
    return translation_transform(get_offset());
}

Transform3d extract_rotation_matrix(const Transform3d& trafo)
{
    SquareMatrix3d rotation;
    SquareMatrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(rotation);
}

Vec3d extract_rotation(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix)
{
    // The extracted "rotation" is a triplet of numbers such that Geometry::rotation_transform
    // returns the original transform. Because of the chosen order of rotations, the triplet
    // is not equivalent to Euler angles in the usual sense.
    Vec3d angles = rotation_matrix.eulerAngles(2,1,0);
    std::swap(angles(0), angles(2));
    return angles;
}

Vec3d extract_rotation(const Transform3d& transform)
{
    // use only the non-translational part of the transform
    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> m = transform.matrix().block(0, 0, 3, 3);
    // remove scale
    m.col(0).normalize();
    m.col(1).normalize();
    m.col(2).normalize();
    return extract_rotation(m);
}

void rotation_transform(Transform3d& transform, const Vec3d& rotation)
{
    transform = Transform3d::Identity();
    transform.rotate(Eigen::AngleAxisd(rotation.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(rotation.x(), Vec3d::UnitX()));
}

Transform3d rotation_transform(const Vec3d& rotation)
{
    Transform3d transform;
    rotation_transform(transform, rotation);
    return transform;
}

Vec3d Transformation::get_rotation() const
{
    return extract_rotation(extract_rotation_matrix(m_matrix));
}

Transform3d Transformation::get_rotation_matrix() const
{
    return extract_rotation_matrix(m_matrix);
}

Transform3d extract_scale(const Transform3d& trafo)
{
    SquareMatrix3d rotation;
    SquareMatrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(scale);
}

std::pair<Transform3d, Transform3d> extract_rotation_scale(const Transform3d& trafo)
{
    SquareMatrix3d rotation;
    SquareMatrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return { Transform3d(rotation), Transform3d(scale) };
}

bool contains_skew(const Transform3d& trafo)
{
    SquareMatrix3d rotation;
    SquareMatrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);

    if (scale.isDiagonal())
      return false;

    if (scale.determinant() >= 0.0)
      return true;

    // the matrix contains mirror
    const SquareMatrix3d ratio = scale.cwiseQuotient(trafo.matrix().block<3,3>(0,0));

    auto check_skew = [&ratio](int i, int j, bool& skew) {
      if (!std::isnan(ratio(i, j)) && !std::isnan(ratio(j, i)))
        skew |= std::abs(ratio(i, j) * ratio(j, i) - 1.0) > EPSILON;
    };

    bool has_skew = false;
    check_skew(0, 1, has_skew);
    check_skew(0, 2, has_skew);
    check_skew(1, 2, has_skew);
    return has_skew;
}

void scale_transform(Transform3d& transform, double scale)
{
    return scale_transform(transform, scale * Vec3d::Ones());
}

void scale_transform(Transform3d& transform, const Vec3d& scale)
{
    transform = Transform3d::Identity();
    transform.scale(scale);
}

Transform3d scale_transform(double scale)
{
    return scale_transform(scale * Vec3d::Ones());
}

Transform3d scale_transform(const Vec3d& scale)
{
    Transform3d transform;
    scale_transform(transform, scale);
    return transform;
}

void Transformation::set_rotation(const Vec3d& rotation)
{
    const Vec3d offset = get_offset();
    m_matrix = rotation_transform(rotation) * extract_scale(m_matrix);
    m_matrix.translation() = offset;
}

void Transformation::set_rotation(Axis axis, double rotation)
{
    rotation = angle_to_0_2PI(rotation);
    if (is_approx(std::abs(rotation), 2.0 * std::numbers::pi_v<double>))
        rotation = 0.0;

    auto [curr_rotation, scale] = extract_rotation_scale(m_matrix);
    Vec3d angles = extract_rotation(curr_rotation);
    angles[axis] = rotation;

    const Vec3d offset = get_offset();
    m_matrix = rotation_transform(angles) * scale;
    m_matrix.translation() = offset;
}

Vec3d Transformation::get_scaling_factor() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return { std::abs(scale(0, 0)), std::abs(scale(1, 1)), std::abs(scale(2, 2)) };
}

Transform3d Transformation::get_scaling_factor_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0) = std::abs(scale(0, 0));
    scale(1, 1) = std::abs(scale(1, 1));
    scale(2, 2) = std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_scaling_factor(const Vec3d& scaling_factor)
{
    assert(scaling_factor.x() > 0.0 && scaling_factor.y() > 0.0 && scaling_factor.z() > 0.0);

    const Vec3d offset = get_offset();
    m_matrix = extract_rotation_matrix(m_matrix) * scale_transform(scaling_factor);
    m_matrix.translation() = offset;
}

void Transformation::set_scaling_factor(Axis axis, double scaling_factor)
{
    assert(scaling_factor > 0.0);

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    scale(axis, axis) = scaling_factor;

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

Vec3d Transformation::get_mirror() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return { scale(0, 0) / std::abs(scale(0, 0)), scale(1, 1) / std::abs(scale(1, 1)), scale(2, 2) / std::abs(scale(2, 2)) };
}

Transform3d Transformation::get_mirror_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0) = scale(0, 0) / std::abs(scale(0, 0));
    scale(1, 1) = scale(1, 1) / std::abs(scale(1, 1));
    scale(2, 2) = scale(2, 2) / std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_mirror(const Vec3d& mirror)
{
    Vec3d copy(mirror);
    const Vec3d abs_mirror = copy.cwiseAbs();
    for (int i = 0; i < 3; ++i) {
        if (abs_mirror(i) == 0.0)
            copy(i) = 1.0;
        else if (abs_mirror(i) != 1.0)
            copy(i) /= abs_mirror(i);
    }

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    const Vec3d curr_scales = { scale(0, 0), scale(1, 1), scale(2, 2) };
    const Vec3d signs = curr_scales.cwiseProduct(copy);

    if (signs[0] < 0.0) scale(0, 0) = -scale(0, 0);
    if (signs[1] < 0.0) scale(1, 1) = -scale(1, 1);
    if (signs[2] < 0.0) scale(2, 2) = -scale(2, 2);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

void Transformation::set_mirror(Axis axis, double mirror)
{
    double abs_mirror = std::abs(mirror);
    if (abs_mirror == 0.0)
        mirror = 1.0;
    else if (abs_mirror != 1.0)
        mirror /= abs_mirror;

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    const double curr_scale = scale(axis, axis);
    const double sign = curr_scale * mirror;

    if (sign < 0.0) scale(axis, axis) = -scale(axis, axis);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

bool Transformation::has_skew() const
{
    return contains_skew(m_matrix);
}

void Transformation::reset()
{
    m_matrix = Transform3d::Identity();
}

void Transformation::reset_rotation()
{
    const TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.v * svd.s * svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_scaling_factor()
{
    const TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_skew()
{
    auto new_scale_factor = [](const SquareMatrix3d& s) {
        return pow(s(0, 0) * s(1, 1) * s(2, 2), 1. / 3.); // scale average
    };

    const TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * scale_transform(new_scale_factor(svd.s)) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

Transform3d Transformation::get_matrix_no_offset() const
{
    Transformation copy(*this);
    copy.reset_offset();
    return copy.get_matrix();
}

Transform3d Transformation::get_matrix_no_scaling_factor() const
{
    Transformation copy(*this);
    copy.reset_scaling_factor();
    return copy.get_matrix();
}

Transform3d Transformation::get_matrix_with_applied_shrinkage_compensation(const Vec3d &shrinkage_compensation) const {
    const Transform3d shrinkage_trafo = scale_transform(shrinkage_compensation);
    const Vec3d trafo_offset         = this->get_offset();
    const Vec3d trafo_offset_xy      = Vec3d(trafo_offset.x(), trafo_offset.y(), 0.);

    Transformation copy(*this);
    copy.set_offset(Axis::X, 0.);
    copy.set_offset(Axis::Y, 0.);

    Transform3d trafo_after_shrinkage    = (shrinkage_trafo * copy.get_matrix());
    trafo_after_shrinkage.translation() += trafo_offset_xy;

    return trafo_after_shrinkage;
}

Transformation Transformation::operator * (const Transformation& other) const
{
    return Transformation(get_matrix() * other.get_matrix());
}

TransformationSVD::TransformationSVD(const Transform3d& trafo)
{
    const auto &m0 = trafo.matrix().block<3, 3>(0, 0);
    mirror = m0.determinant() < 0.0;

    SquareMatrix3d m;
    if (mirror)
        m = m0 * Eigen::DiagonalMatrix<double, 3, 3>(-1.0, 1.0, 1.0);
    else
        m = m0;
    const Eigen::JacobiSVD<SquareMatrix3d> svd(m, Eigen::ComputeFullU | Eigen::ComputeFullV);
    u = svd.matrixU();
    v = svd.matrixV();
    s = svd.singularValues().asDiagonal();

    scale = !s.isApprox(SquareMatrix3d::Identity());
    anisotropic_scale = ! is_approx(s(0, 0), s(1, 1)) || ! is_approx(s(1, 1), s(2, 2));
    rotation = !v.isApprox(u);

    if (anisotropic_scale) {
        rotation_90_degrees = true;
        for (int i = 0; i < 3; ++i) {
            const Vec3d row = v.row(i).cwiseAbs();
            const size_t num_zeros = is_approx(row[0], 0.) + is_approx(row[1], 0.) + is_approx(row[2], 0.);
            const size_t num_ones  = is_approx(row[0], 1.) + is_approx(row[1], 1.) + is_approx(row[2], 1.);
            if (num_zeros != 2 || num_ones != 1) {
                rotation_90_degrees = false;
                break;
            }
        }
        // Detect skew by brute force: check if the axes are still orthogonal after transformation
        const SquareMatrix3d trafo_linear = trafo.linear();
        const std::array<Vec3d, 3> axes = { Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ() };
        std::array<Vec3d, 3> transformed_axes;
        for (int i = 0; i < 3; ++i) {
            transformed_axes[i] = trafo_linear * axes[i];
        }
        skew = std::abs(transformed_axes[0].dot(transformed_axes[1])) > EPSILON ||
               std::abs(transformed_axes[1].dot(transformed_axes[2])) > EPSILON ||
               std::abs(transformed_axes[2].dot(transformed_axes[0])) > EPSILON;

        // This following old code does not work under all conditions. The v matrix can become non diagonal (see SPE-1492) 
//        skew = ! rotation_90_degrees;
    } else
        skew = false;
}
}
