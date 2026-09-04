#pragma once

#include <memory>

#include "Slic3r/App/Scene/Transform.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/Biz/Platform/ListenerList.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App::Platform {
struct CameraSynchData;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Scene {

class Camera;

enum class CameraProjectionType : uint8_t
{
    Perspective,
    Orthographic
};

/**
 * @brief Interface for camera projection computation.
 */
class AbstractCameraProjection
{
public:
    explicit AbstractCameraProjection(CameraProjectionType type) : m_type(type) {}

    AbstractCameraProjection(CameraProjectionType type, double z_near, double z_far) :
        m_type(type),
        m_z_near(z_near),
        m_z_far(z_far)
    {}

    virtual ~AbstractCameraProjection() = default;

    /**
     * @brief Compute projection matrix for given @p viewport
     * @param viewport Camera viewport position and size
     * @param zoom Zoom value to apply
     * @return Projection matrix
     *
     * @note
     * Zoom:
     * - for perspective projection the zoom modifies the vertical field of view by applying a scaling factor equal to 1/zoom
     * - for orthographic projection the zoom modifies the viewport by applying a scaling factor equal to 1/zoom
     */
    virtual Domain::SquareMatrix4d projection(const Render::Rect& viewport, double zoom) const = 0;

    virtual double min_zoom() const = 0;
    virtual double max_zoom() const = 0;

    /**
     * @brief Compute screen space size scale.
     *
     * This scale is applied by ScreenSpaceSizedTransformModifier to keep screen space size constant
     * for a given node.
     *
     * @param cam A camera object
     * @param cam_object_dist A distance from camera to node.
     * @return Scale to be applied
     *
     * @note
     * Typically:
     * - for perspective projection this should be @f(\frac{dist}{2 \cdot tan \frac{fov_y}{2} }@f)
     * (where @f(dist@f) is camera-node disance, and @f(fov_y@f) is Field of view in vertical axis),
     * - for ortho projection this should be @f(\frac{2}{r - l}@f) (where @f(l@f) and @f(r@f) are
     * left resp. right parameters of ortho projection).
     * .
     */
    virtual double constant_screen_space_size_scale(const Camera& cam, double cam_object_dist) const = 0;

    /**
     * @brief Get the type of projection
     * @return The type of projection
     */
    CameraProjectionType type() const
    {
        return m_type;
    }

    /**
     * @brief Get the distance of the near plane from the camera eye
     * @return The distance of the near plane from the camera eye
     */
    double z_near() const
    {
        return m_z_near;
    }

    /**
     * @brief Set the distance of the near plane from the camera eye with the given value
     * @param val The new value for the distance of the near plane from the camera eye
     */
    void set_z_near(double val)
    {
        m_z_near = val;
    }

    /**
     * @brief Get the distance of the far plane from the camera eye
     * @return The distance of the far plane from the camera eye
     */
    double z_far() const
    {
        return m_z_far;
    }

    /**
     * @brief Set the distance of the far plane from the camera eye with the given value
     * @param val The new value for the distance of the far plane from the camera eye
     */
    void set_z_far(double val)
    {
        m_z_far = val;
    }

protected:
    CameraProjectionType m_type;
    double m_z_near{10.};
    double m_z_far{1000.};
};

class ICameraUpdateListener
{
public:
    virtual ~ICameraUpdateListener() = default;

    virtual void camera_updated(const Camera& cam) = 0;
};

class Camera : public WithListeners<ICameraUpdateListener>
{
public:
    Camera();

    const Transform& model() const
    {
        return m_model;
    }

    void set_model(const Transform& m);

    void look_at(const Domain::Vec3d& eye, const Domain::Vec3d& center, const Domain::Vec3d& up);

    const Domain::SquareMatrix4d& projection() const
    {
        return m_projection;
    }

    void set_projection(const Domain::SquareMatrix4d& m);

    void switch_projection_type();

    Transform view() const
    {
        return m_model.inverse();
    }

    Domain::Vec3d position() const
    {
        return m_model.matrix().block<3, 1>(0, 3);
    }

    Domain::Vec3d forward() const
    {
        return -m_model.matrix().block<3, 1>(0, 2);
    }

    Domain::Vec3d right() const
    {
        return m_model.matrix().block<3, 1>(0, 0);
    }

    Domain::Vec3d up() const
    {
        return m_model.matrix().block<3, 1>(0, 1);
    }

    void set_viewport(const Render::Rect& viewport);

    const Render::Rect& viewport() const
    {
        return m_viewport;
    }

    void set_zoom(double value);

    void update_zoom(double value)
    {
        set_zoom(m_zoom / (1.0 - std::max(std::min(value, 4.0), -4.0) * 0.1));
    }

    double zoom() const
    {
        return m_zoom;
    }

    void set_z_far(double z)
    {
        m_projection_getter->set_z_far(z);
        update_projection();
    }

    void set_z_near(double z)
    {
        m_projection_getter->set_z_near(z);
        update_projection();
    }

    void set_z_near_far(double z_near, double z_far)
    {
        m_projection_getter->set_z_near(z_near);
        m_projection_getter->set_z_far(z_far);
        update_projection();
    }

    Ray ray_at(double screen_x, double screen_y) const;
    Domain::Vec3d unproject(const Domain::Vec3d& win_pos) const;
    Domain::Vec3d project_to_ndc(const Domain::Vec3d& world_pos) const;
    Domain::Vec2d project_to_screen_space(const Domain::Vec3d& world_pos) const;

    const AbstractCameraProjection& cam_projection() const
    {
        return *m_projection_getter;
    }

    bool pointing_upward() const
    {
        return (m_projection_getter->type() == CameraProjectionType::Perspective) ?
            position().z() < 0.0 :
            forward().z() >= 0.0;
    }

    void update_synch_data(Platform::CameraSynchData& data) const;
    void synchronize_from(const Platform::CameraSynchData& data);

private:
    void update_projection();

private:
    using CameraUpdateListeners = Biz::ListenerList<ICameraUpdateListener>;

    Transform m_model{Transform::Identity()};
    Domain::SquareMatrix4d m_projection{Domain::SquareMatrix4d::Identity()};
    Render::Rect m_viewport;
    double m_zoom{1.};
    std::unique_ptr<AbstractCameraProjection> m_projection_getter;
};

class PerspectiveCameraProjection : public AbstractCameraProjection
{
public:
    PerspectiveCameraProjection();

    Domain::SquareMatrix4d projection(const Render::Rect& viewport, double zoom) const override;
    double constant_screen_space_size_scale(const Camera& cam, double cam_object_dist) const override;

    double fovy() const
    {
        return m_fovy;
    }

    double min_zoom() const override;
    double max_zoom() const override;

private:
    const double m_fovy;
};

class OrthographicCameraProjection : public AbstractCameraProjection
{
public:
    OrthographicCameraProjection() : AbstractCameraProjection(CameraProjectionType::Orthographic) {}

    OrthographicCameraProjection(double z_near, double z_far) :
        AbstractCameraProjection(CameraProjectionType::Orthographic, z_near, z_far)
    {}

    Domain::SquareMatrix4d projection(const Render::Rect& viewport, double zoom) const override;
    double constant_screen_space_size_scale(const Camera& cam, double cam_object_dist) const override;

    double min_zoom() const override;
    double max_zoom() const override;
};

} // namespace Slic3r::App::Scene
