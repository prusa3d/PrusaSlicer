#include "Slic3r/App/CubeView.hpp"

#include "Slic3r/App/Imgui/NavigationCube.hpp"

#include <imgui/imgui.h>

namespace Slic3r::App {

static constexpr float SIZE = 80.0f;

CubeView::CubeView() : Window("CubeView") {
    set_flags(flags() | ImGuiWindowFlags_NoBackground);
    set_width(SIZE);
    set_height(SIZE);
    set_aspect_ratio(1.0);
}

void CubeView::render_body(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    DEBUG_ASSERT(m_camera != nullptr);
    DEBUG_ASSERT(m_trackball != nullptr);

    Domain::SquareMatrix4f view = m_camera->view().matrix().cast<float>();
    Domain::SquareMatrix4f proj = m_camera->projection().matrix().cast<float>();
    float cam_distance = m_trackball->distance_to_target();
    ImVec2 im_pos = ImVec2(pos.x(), pos.y());
    ImVec2 im_size = ImVec2(size.x(), size.y());

    Imgui::NavCube::set_draw_list(ImGui::GetWindowDrawList());
    Imgui::NavCube::set_orthographic(m_camera->cam_projection().type() == Scene::CameraProjectionType::Orthographic);
    Imgui::NavCube::view_manipulate(view, proj, cam_distance, im_pos, im_size, 0);
    if (Imgui::NavCube::is_animation_running()) {
        auto [azimuth, zenith] = Imgui::NavCube::get_azimuth_and_zenith();
        m_trackball->set_azimuth_and_zenith(azimuth, zenith);
        // requires extra frames for animation
        m_require_render = true;
    }
    else
        m_require_render = false;
}

}// Slic3r::App namespace
