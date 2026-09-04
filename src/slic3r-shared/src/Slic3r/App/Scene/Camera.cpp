#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/CameraProjectionParameters.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"

using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec4d;

namespace Slic3r::App::Scene {

Camera::Camera()
    : m_model(Transform::Identity()), m_projection_getter(new PerspectiveCameraProjection)
{}

void Camera::set_model(const Transform& m)
{
    m_model = m;
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

void Camera::set_projection(const Domain::SquareMatrix4d& m)
{
    m_projection = m;
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

void Camera::set_viewport(const Render::Rect& viewport)
{
    m_viewport = viewport;
    update_projection();
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

void Camera::set_zoom(double value)
{
    m_zoom = std::clamp(value, m_projection_getter->min_zoom(), m_projection_getter->max_zoom());
    update_projection();
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

void Camera::look_at(const Vec3d& eye, const Vec3d& center, const Vec3d& up)
{
    m_model = Render::look_at(eye, center, up).inverse();
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

void Camera::switch_projection_type()
{
    DEBUG_ASSERT(m_projection_getter != nullptr);
    if (m_projection_getter->type() == CameraProjectionType::Perspective) {
        m_projection_getter.reset(new OrthographicCameraProjection);
        m_zoom = CameraProjectionParameters::orthographic_zoom_from_perspective(m_zoom);
    }
    else {
        m_projection_getter.reset(new PerspectiveCameraProjection);
        m_zoom = CameraProjectionParameters::perspective_zoom_from_orthographic(m_zoom);
    }
    update_projection();
    invoke_listeners<ICameraUpdateListener>([this](auto* l) { l->camera_updated(*this); });
}

Ray Camera::ray_at(double screen_x, double screen_y) const
{
    screen_x -= m_viewport.x;
    screen_y -= m_viewport.y;

    Vec3d ray_nds{
        (2.0 * screen_x) / m_viewport.width - 1.0,
        1.0 - (2.0 * screen_y) / m_viewport.height,
        1
    };
    Vec4d ray_clip{ray_nds.x(), ray_nds.y(), -1, 1};
    Vec4d ray_eye = m_projection.inverse() * ray_clip;
    if (m_projection_getter->type() == CameraProjectionType::Perspective) {
        ray_eye.z() = -1;
        ray_eye.w() = 0;

        Vec3d ray_world = (m_model * ray_eye).head<3>();

//        SPDLOG_INFO("ray NDS ({},  {},  {})", ray_nds.x(), ray_nds.y(), ray_nds.z());
//        SPDLOG_INFO("ray clip ({},  {},  {},  {})", ray_clip.x(), ray_clip.y(), ray_clip.z(), ray_clip.w());
//        SPDLOG_INFO("ray eye ({},  {},  {},  {})", ray_eye.x(), ray_eye.y(), ray_eye.z(), ray_eye.w());
//        SPDLOG_INFO("ray world ({},  {},  {})", ray_world.x(), ray_world.y(), ray_world.z());

        return {m_model.matrix().block<3, 1>(0, 3), ray_world.normalized()};
    }
    else {
        Vec4d ray_origin_eye(ray_eye.x(), ray_eye.y(), 0, 1);
        return {(m_model * ray_origin_eye).head<3>(), forward()};
    }
}

Vec3d Camera::unproject(const Vec3d& win_pos) const
{
    SquareMatrix4d inv_pm = (m_projection * view().matrix()).inverse();
    Vec4d w{
        (2 * win_pos.x() - m_viewport.x) / m_viewport.width - 1,
        (2 * win_pos.y() - m_viewport.y) / m_viewport.height - 1,
        2 * win_pos.z() - 1,
        1
    };
    Vec4d p = inv_pm * w;
    return p.head<3>() / p.w();
}

Domain::Vec3d Camera::project_to_ndc(const Domain::Vec3d& world_pos) const
{
    const Domain::SquareMatrix4d projection_view_matrix = m_projection * view().matrix();
    // world to clip
    Vec4d clip = projection_view_matrix * Vec4d(world_pos.x(), world_pos.y(), world_pos.z(), 1.0);
    // clip to ndc
    return Vec3d(clip.x(), clip.y(), clip.z()) / clip.w();
}

Vec2d Camera::project_to_screen_space(const Vec3d& world_pos) const
{
    // world to ndc
    Vec3d ndc = project_to_ndc(world_pos);
    // ndc to ss
    double half_w = 0.5 * double(m_viewport.width);
    double half_h = 0.5 * double(m_viewport.height);
    return { half_w * ndc.x() + double(m_viewport.x) + half_w, half_h * ndc.y() + double(m_viewport.y) + half_h };
}

void Camera::update_synch_data(Platform::CameraSynchData& data) const
{
    data.type  = uint8_t(cam_projection().type());
    data.model = model();
    data.zoom  = zoom();
}

void Camera::synchronize_from(const Platform::CameraSynchData& data)
{
    if (uint8_t(cam_projection().type()) != data.type)
        switch_projection_type();

    set_model(data.model);
    set_zoom(data.zoom);
}

void Camera::update_projection()
{
    DEBUG_ASSERT(m_projection_getter != nullptr);
    m_projection = m_projection_getter->projection(m_viewport, m_zoom);
}

PerspectiveCameraProjection::PerspectiveCameraProjection() :
    AbstractCameraProjection(CameraProjectionType::Perspective),
    m_fovy{CameraProjectionParameters::REF_FOVY}
{}

Domain::SquareMatrix4d PerspectiveCameraProjection::projection(const Render::Rect& viewport, double zoom) const
{
    DEBUG_ASSERT(zoom != 0.0);
    return Render::perspective(m_fovy / zoom, double(viewport.width) / double(viewport.height), m_z_near, m_z_far);
}

double PerspectiveCameraProjection::constant_screen_space_size_scale(
    const Camera& cam, double cam_object_dist
) const
{

    double phi_half = m_fovy / (2 * cam.zoom());
    /*
    // TODO: This needs to be checked
    double denom = 2 * std::tan(deg2rad(phi_half));
    double ret = cam_object_dist / denom;
    SPDLOG_INFO("Screen scale: {}   dist: {}  fovy (actual): {}  fovy (zoomed): {}", ret, cam_object_dist, m_fovy, phi_half);
    return ret;
    */
    return cam_object_dist/2 * std::tan(deg2rad(phi_half));
}

double PerspectiveCameraProjection::min_zoom() const
{
    return CameraProjectionParameters::PERSPECTIVE_MIN_ZOOM;
}

double PerspectiveCameraProjection::max_zoom() const
{
    return CameraProjectionParameters::PERSPECTIVE_MAX_ZOOM;
}

Domain::SquareMatrix4d OrthographicCameraProjection::projection(const Render::Rect& viewport, double zoom) const
{
    ASSERT(zoom != 0.0);
    double inv_zoom = 1.0 / zoom;
    // double half_w = 0.5 * inv_zoom * viewport.width;
    // double half_h = 0.5 * inv_zoom * viewport.height;
    double half_h = inv_zoom;
    double half_w = double(viewport.width) / viewport.height * half_h;
    return Render::ortho(-half_w, half_w, -half_h, half_h, m_z_near, m_z_far);
}

double OrthographicCameraProjection::constant_screen_space_size_scale(const Camera& cam, double cam_object_dist) const
{
    // TODO: This needs to be checked
    //return 2 * cam.zoom() / (cam.viewport().width);
    //return cam_object_dist / (2 * std::tan(deg2rad(m_fovy / 2)));
    // Note: For orhto this is: 2 / (r - l)
    return 0.5/cam.zoom();

}

double OrthographicCameraProjection::min_zoom() const
{
    return CameraProjectionParameters::orthographic_min_zoom();
}

double OrthographicCameraProjection::max_zoom() const
{
    return CameraProjectionParameters::orthographic_max_zoom();
}

} // namespace Slic3r::App::Scene
