#include <algorithm>
#include <cmath>

#include "Slic3r/App/Scene/CameraTrackballController.hpp"
#include "Slic3r/App/Scene/CameraProjectionParameters.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;

using namespace Slic3r::Biz;

namespace Slic3r::App::Scene {

CameraTrackballController::CameraTrackballController(Camera& camera) :
    m_camera(camera),
    m_distance{CameraProjectionParameters::REF_Z}
{
    set_camera_orientation();
}

void CameraTrackballController::reset_distance_to_target()
{
    m_distance = CameraProjectionParameters::REF_Z;
}

void CameraTrackballController::add_azimuth_and_zenith(double delta_azimuth, double delta_zenith, bool apply_limits)
{
    delta_zenith = fmod(delta_zenith, 2.0 * M_PI);
    m_zenith += delta_zenith;
    if (apply_limits) {
        if (m_zenith < 0.0) {
            delta_zenith -= m_zenith;
            m_zenith = 0.0;
        }
        else if (m_zenith > M_PI) {
            delta_zenith -= m_zenith - M_PI;
            m_zenith = M_PI;
        }
    }

    delta_azimuth = fmod(delta_azimuth, 2.0 * M_PI);
    m_azimuth += delta_azimuth;
    if (m_azimuth < 0.0)
        m_azimuth += 2.0 * M_PI;
    else if (m_azimuth > 2.0 * M_PI)
        m_azimuth -= 2.0 * M_PI;

    Transform3d view = Transform3d(m_camera.view());
    Vec3d translation = view.translation() + m_view_rotation * m_pivot;
    Vec3d old_eye_target = view * m_target;

    auto rot_z = Eigen::AngleAxisd(delta_azimuth, Vec3d::UnitZ());
    m_view_rotation *= rot_z * Eigen::AngleAxisd(delta_zenith, rot_z.inverse() * m_camera.right());
    m_view_rotation.normalize();

    view.fromPositionOrientationScale(m_view_rotation * (-m_pivot) + translation, m_view_rotation, Vec3d::Ones());

    Transform3d model = view.inverse();
    m_camera.set_model(model);
    m_target = model * old_eye_target;
}

void CameraTrackballController::rotate_around_pivot(const Vec3d& rotation_in_camera_axes)
{
    const double angle = rotation_in_camera_axes.norm();
    if (angle <= Domain::EPSILON) {
        return;
    }

    Transform3d view           = Transform3d(m_camera.view());
    const Vec3d translation    = view.translation() + m_view_rotation * m_pivot;
    const Vec3d old_eye_target = view * m_target;
    const Vec3d rotation_axis  = m_view_rotation.conjugate() * rotation_in_camera_axes.normalized();

    m_view_rotation *= Eigen::Quaterniond(Eigen::AngleAxisd(angle, rotation_axis));
    m_view_rotation.normalize();
    view.fromPositionOrientationScale(
        m_view_rotation * (-m_pivot) + translation,
        m_view_rotation,
        Vec3d::Ones()
    );

    const Transform3d model = view.inverse();
    m_camera.set_model(model);
    m_target = model * old_eye_target;
    this->update_azimuth_and_zenith_from_camera();
}

void CameraTrackballController::recover_from_free_rotation()
{
    if (std::abs(m_camera.right().z()) > Domain::EPSILON) {
        this->set_camera_orientation();
    }
}

void CameraTrackballController::update_synch_data(Platform::CameraSynchData& data) const
{
    data.target        = target();
    data.pivot         = pivot();
    data.view_rotation = view_rotation();
    data.distance      = distance_to_target();
    data.azimuth       = azimuth();
    data.zenith        = zenith();
}

void CameraTrackballController::synchronize_from(const Platform::CameraSynchData& data)
{
    set_target(data.target);
    set_pivot(data.pivot);
    set_distance_to_target(data.distance);
    set_azimuth_and_zenith(data.azimuth, data.zenith);
    set_view_rotation(data.view_rotation);
}

void CameraTrackballController::set_camera_orientation()
{
    double cos_a = cos(m_azimuth);
    double sin_a = sin(m_azimuth);
    double cos_z = cos(m_zenith);
    double sin_z = sin(m_zenith);

    Vec3d xyz = { sin_z * cos_a, sin_z * sin_a, cos_z };
    Vec3d off = m_distance * xyz;
    Vec3d up = (std::abs(Algorithms::Point::to_2d(xyz).norm()) > Domain::EPSILON) ? Vec3d::UnitZ() :
        (std::abs(m_zenith) < Domain::EPSILON) ? Vec3d(-cos_a, -sin_a, 0.0f) : Vec3d(cos_a, sin_a, 0.0f);
    Vec3d pos = m_target - off;

    m_camera.look_at(pos, m_target, up);
    m_view_rotation = Eigen::Quaterniond(m_camera.view().matrix().block<3, 3>(0, 0));
}

void CameraTrackballController::normalize_azimuth_and_zenith()
{
    m_zenith = std::clamp(fmod(m_zenith, 2.0 * M_PI), 0.0, M_PI);
    m_azimuth = fmod(m_azimuth, 2.0 * M_PI);
    if (m_azimuth < 0.0)
        m_azimuth += 2.0 * M_PI;
}

void CameraTrackballController::update_azimuth_and_zenith_from_camera()
{
    const Vec3d forward = m_camera.forward();
    m_zenith            = std::acos(std::clamp(forward.z(), -1.0, 1.0));

    Vec3d azimuth_direction = forward;
    if (Algorithms::Point::to_2d(forward).norm() <= Domain::EPSILON) {
        // At the poles set_camera_orientation() encodes the azimuth into the up vector.
        azimuth_direction = m_zenith < M_PI_2 ? Vec3d(-m_camera.up()) : m_camera.up();
    }

    m_azimuth = std::atan2(azimuth_direction.y(), azimuth_direction.x());
    this->normalize_azimuth_and_zenith();
}

} // namespace Slic3r::App::Scene
