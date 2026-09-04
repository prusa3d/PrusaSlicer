#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"

namespace Slic3r::App {

class CubeView : public Yoga::Window
{
public:
    CubeView();

    void set_camera_data(Scene::Camera& camera, Scene::CameraTrackballController& trackball) {
        m_camera = &camera;
        m_trackball = &trackball;
    }

    void render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;
    bool require_render() const { return m_require_render; }

private:
    Scene::Camera* m_camera{nullptr};
    Scene::CameraTrackballController* m_trackball{nullptr};
    bool m_require_render{false};
};

} // namespace Slic3r::App
