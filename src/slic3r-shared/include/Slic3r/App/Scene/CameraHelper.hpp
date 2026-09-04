#pragma once

#include <Slic3r/App/Render/Geometry.hpp>

#define ENABLED_DEBUG_CAMERA 0

namespace Slic3r::App::Platform {
struct CameraSynchData;
} // namespace Slic3r::App::Platform

namespace Slic3r::Domain {
class Project;
struct BedRef;
} // namespace Slic3r::Domain

namespace Slic3r::App::Platform {
class AnimationManager;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Scene {

class Camera;
class CameraTrackballController;

void zoom_to_box(Camera& camera, const Eigen::AlignedBox3d& aabb);
void synchronize_camera(const Platform::CameraSynchData& data, Camera& camera, CameraTrackballController& trackball);
void center_camera_on_bed(const Domain::Project& project, const Domain::BedRef& bed_ref, CameraTrackballController& trackball);
void animated_center_camera_on_bed(const Domain::Project& project, const Domain::BedRef& bed_ref, CameraTrackballController& trackball,
    Platform::AnimationManager& animation_manager, double duration_in_sec = 0.25);

#if ENABLED_DEBUG_CAMERA
void render_imgui_debug_camera(const Camera& camera, const CameraTrackballController& trackball);
#endif // ENABLED_DEBUG_CAMERA

} // namespace Slic3r::App::Scene
