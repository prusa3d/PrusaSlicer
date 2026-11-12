#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"
#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/App/Platform/AnimationManager.hpp"
#include "Slic3r/App/Scene/CameraTargetAnimation.hpp"

#if ENABLED_DEBUG_CAMERA
#include <imgui/imgui.h>
#endif // ENABLED_DEBUG_CAMERA

#include <cfloat>

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Scene {

void zoom_to_box(Camera& camera, const Eigen::AlignedBox3d& aabb)
{
    const AbstractCameraProjection& proj = camera.cam_projection();
    Transform3d view_xform               = Transform3d(camera.view());

    if (proj.type() == CameraProjectionType::Perspective) {
        double max_angle_x = -DBL_MAX;
        double max_angle_y = -DBL_MAX;
        for (int i = 0; i < 8; ++i) {
            Vec3d eye_corner = view_xform * aabb.corner(Eigen::AlignedBox3d::CornerType(i));
            max_angle_x      = std::
                max(max_angle_x, std::atan2(std::abs(eye_corner.x()), std::abs(eye_corner.z())));
            max_angle_y = std::
                max(max_angle_y, std::atan2(std::abs(eye_corner.y()), std::abs(eye_corner.z())));
        }

        // evaluate and apply the new zoom factor
        const Render::Rect& viewport = camera.viewport();
        double fov_y                 = deg2rad(
            0.5 * reinterpret_cast<const Scene::PerspectiveCameraProjection*>(&proj)->fovy()
        );
        double fov_x = std::atan(double(viewport.width) / (double(viewport.height) * std::tan(fov_y)));
        camera.set_zoom(1.0 / std::max(max_angle_x / fov_x, max_angle_y / fov_y));
    } else {
        double max_x = -DBL_MAX;
        double max_y = -DBL_MAX;
        for (int i = 0; i < 8; ++i) {
            Vec3d eye_corner = view_xform * aabb.corner(Eigen::AlignedBox3d::CornerType(i));
            max_x            = std::max(max_x, std::abs(eye_corner.x()));
            max_y            = std::max(max_y, std::abs(eye_corner.y()));
        }
        double aabb_aspect_ratio     = max_x / max_y;
        const Render::Rect& viewport = camera.viewport();
        double aspect_ratio          = double(viewport.width) / double(viewport.height);
        double h = (aabb_aspect_ratio > aspect_ratio) ? max_x / aspect_ratio : max_y;
        static constexpr double MARGIN_FACTOR = 1.05f;
        camera.set_zoom(1.0 / (h * MARGIN_FACTOR));
    }
}

void synchronize_camera(const Platform::CameraSynchData& data, Camera& camera, CameraTrackballController& trackball)
{
    trackball.synchronize_from(data);
    camera.synchronize_from(data);
}

void center_camera_on_bed(const Domain::Project& project, const Domain::BedRef& bed_ref, CameraTrackballController& trackball)
{
    // Center the camera on the given bed
    const Domain::ConfigContainer* cc = project.find_config_container(bed_ref.config_container_id);
    DEBUG_ASSERT(cc != nullptr);
    const Domain::BedInstance& inst = cc->find_bed_instance(bed_ref.instance_id);
    Vec3d selected_bed_center = Biz::Algorithms::Point::to_3d(cc->bed().center(), 0.0) + inst.transformation.get_offset();
    trackball.set_target(selected_bed_center);
    trackball.synchronize_pivot_with_target();
}

void animated_center_camera_on_bed(const Domain::Project& project, const Domain::BedRef& bed_ref, CameraTrackballController& trackball,
    Platform::AnimationManager& animation_manager, double duration_in_sec)
{
    // allow only one animation per trackball running at a time
    static std::map<CameraTrackballController*, CameraTargetAnimation*> animation_cache;
    auto it = animation_cache.find(&trackball);
    if (it != animation_cache.end()) {
        if (animation_manager.state(it->second) == Platform::AnimationState::Running)
            it->second->stop();
        animation_cache.erase(it);
    }

    // remove from the cache all expired animations
    std::erase_if(animation_cache, [&](const auto& item) {
        return animation_manager.state(item.second) == Platform::AnimationState::Undefined;
    });

    Domain::Vec3d start_position = trackball.target();
    const Domain::ConfigContainer* cc = project.find_config_container(bed_ref.config_container_id);
    DEBUG_ASSERT(cc != nullptr);
    const Domain::BedInstance& inst = cc->find_bed_instance(bed_ref.instance_id);
    Domain::Vec3d final_position = Biz::Algorithms::Point::to_3d(cc->bed().center(), 0.0) + inst.transformation.get_offset();
    if (!final_position.isApprox(start_position))
        animation_cache[&trackball] =
            &animation_manager.add_animation<CameraTargetAnimation>(trackball, start_position, final_position, duration_in_sec);
}

#if ENABLED_DEBUG_CAMERA
void render_imgui_debug_camera(const Camera& camera, const CameraTrackballController& trackball)
{
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    if (ImGui::Begin("Camera debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("Camera", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Type");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", (camera.cam_projection().type() == CameraProjectionType::Orthographic) ? "Orthographic" : "Perspective");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Viewport");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(
                "%d, %d, %d, %d",
                camera.viewport().x,
                camera.viewport().y,
                camera.viewport().width,
                camera.viewport().height
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Position");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(
                "%.3f, %.3f, %.3f",
                camera.position().x(),
                camera.position().y(),
                camera.position().z()
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Target");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(
                "%.3f, %.3f, %.3f",
                trackball.target().x(),
                trackball.target().y(),
                trackball.target().z()
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Distance to target");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", trackball.distance_to_target());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Pivot");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(
                "%.3f, %.3f, %.3f",
                trackball.pivot().x(),
                trackball.pivot().y(),
                trackball.pivot().z()
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Azimuth");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", rad2deg(trackball.azimuth()));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Zenith");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", rad2deg(trackball.zenith()));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Forward");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(
                "%.3f, %.3f, %.3f",
                camera.forward().x(),
                camera.forward().y(),
                camera.forward().z()
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Right");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f, %.3f, %.3f", camera.right().x(), camera.right().y(), camera.right().z());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Up");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f, %.3f, %.3f", camera.up().x(), camera.up().y(), camera.up().z());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Near Z");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", camera.cam_projection().z_near());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Far Z");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", camera.cam_projection().z_far());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("View rotation");
            ImGui::TableSetColumnIndex(1);
            const Eigen::Quaterniond& view_rotation = trackball.view_rotation();
            ImGui::Text(
                "%.3f, %.3f, %.3f, %.3f",
                view_rotation.x(),
                view_rotation.y(),
                view_rotation.z(),
                view_rotation.w()
            );

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Zoom");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", camera.zoom());

            auto& proj = camera.cam_projection();
            if (proj.type() == Scene::CameraProjectionType::Perspective) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("FOVy");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", dynamic_cast<const Scene::PerspectiveCameraProjection&>(proj).fovy());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("FOVy/Zoom");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text(
                    "%.3f",
                    dynamic_cast<const Scene::PerspectiveCameraProjection&>(proj).fovy() / camera.zoom()
                );
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}
#endif // ENABLED_DEBUG_CAMERA

} // namespace Slic3r::App::Scene
