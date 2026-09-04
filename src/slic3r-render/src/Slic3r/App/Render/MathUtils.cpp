#include "Slic3r/App/Render/MathUtils.hpp"

#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::SquareMatrix4f;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec3f;

namespace Slic3r::App::Render {

template <typename T>
using Vec3 = Eigen::Matrix<T, 3, 1>;

template <typename T>
using Vec2 = Eigen::Matrix<T, 2, 1>;

template <typename T>
using Matrix4 = Eigen::Matrix<T, 4, 4>;

template <typename T>
Matrix4<T> ortho(T left, T right, T bottom, T top, T near_z, T far_z)
{
    Matrix4<T> ret;
    ret <<
        2 / (right - left), 0, 0, - (right + left) / (right - left),
        0, 2 / (top - bottom), 0, - (top + bottom) / (top - bottom),
        0, 0, - 2 / (far_z - near_z), - (far_z + near_z) / (far_z - near_z),
        0, 0, 0, 1;
    return ret;
}

SquareMatrix4d ortho(double left, double right, double bottom, double top, double near_z, double far_z)
{ return ortho<double>(left, right, bottom, top, near_z, far_z); }

SquareMatrix4f ortho(float left, float right, float bottom, float top, float near_z, float far_z)
{ return ortho<float>(left, right, bottom, top, near_z, far_z); }


template <typename T>
Matrix4<T> frustum(T left, T right, T bottom, T top, T near_z, T far_z)
{
    const T inv_dx = T(1.0) / (right - left);
    const T inv_dy = T(1.0) / (top - bottom);
    const T inv_dz = T(1.0) / (far_z - near_z);
    Matrix4<T> ret;
    ret <<
        2.0 * near_z * inv_dx, 0.0,    (left + right) * inv_dx,                             0.0,
        0.0, 2.0 * near_z * inv_dy,    (bottom + top) * inv_dy,                             0.0,
        0.0,                    0.0, -(near_z + far_z) * inv_dz, -2.0 * near_z * far_z * inv_dz,
        0.0,                    0.0,                       -1.0,                             0.0;
    return ret;
}

SquareMatrix4f frustum(float left, float right, float bottom, float top, float near_z, float far_z)
{ return frustum<float>(left, right, bottom, top, near_z, far_z); }

SquareMatrix4d frustum(double left, double right, double bottom, double top, double near_z, double far_z)
{ return frustum<double>(left, right, bottom, top, near_z, far_z); }


template <typename T>
Matrix4<T> perspective(T fovy, T aspect, T near_z, T far_z)
{
    const T f = T(1.0) / std::tan(deg2rad(fovy) / 2);
    const double dist_z = near_z - far_z;
    Matrix4<T> ret;
    ret <<
        f/aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far_z + near_z) / dist_z, 2 * far_z * near_z / dist_z,
        0, 0, -1, 0;
    return ret;
}

SquareMatrix4f perspective(float fovy, float aspect, float near_z, float far_z)
{ return perspective<float>(fovy, aspect, near_z, far_z); }

SquareMatrix4d perspective(double fovy, double aspect, double near_z, double far_z)
{ return perspective<double>(fovy, aspect, near_z, far_z); }


template <typename T>
Matrix4<T> look_at(const Vec3<T>& eye, const Vec3<T>& center, const Vec3<T>& up)
{
    Vec3<T> f = (center - eye).normalized();
    Vec3<T> u = up.normalized();
    Vec3<T> s = f.cross(u).normalized();
    u = s.cross(f);

    Matrix4<T> ret = Matrix4<T> ::Identity();
    ret(0,0) = s.x();
    ret(0, 1) = s.y();
    ret(0, 2) = s.z();
    ret(0, 3) = -s.dot(eye);

    ret(1, 0) = u.x();
    ret(1, 1) = u.y();
    ret(1, 2) = u.z();
    ret(1, 3) = -u.dot(eye);

    ret(2, 0) = -f.x();
    ret(2, 1) = -f.y();
    ret(2, 2) = -f.z();
    ret(2, 3) = f.dot(eye);

    return ret;
}

SquareMatrix4f look_at(const Vec3f& eye, const Vec3f& center, const Vec3f& up)
{ return look_at<float>(eye, center, up); }

SquareMatrix4d look_at(const Vec3d& eye, const Vec3d& center, const Vec3d& up)
{ return look_at<double>(eye, center, up); }


template <typename T>
Vec2<T> viewport_transform(const Rect& viewport, const Vec3<T>& ndc_pos)
{
    T viewport_half_width = viewport.width / T(2.0);
    T viewport_half_height = viewport.height / T(2.0);
    return {
        viewport_half_width * ndc_pos.x() + viewport.x + viewport_half_width,
        viewport_half_height * ndc_pos.y() + viewport.y + viewport_half_height
    };
}

Vec2f viewport_transform(const Rect& viewport, const Vec3f& ndc_pos)
{ return viewport_transform<float>(viewport, ndc_pos); }

Vec2d viewport_transform(const Rect& viewport, const Vec3d& ndc_pos)
{ return viewport_transform<double>(viewport, ndc_pos); }


} // namespace Slic3r::App::Render
