#include "Slic3r/App/Scene/CameraTargetAnimation.hpp"

using namespace std::chrono;

namespace Slic3r::App::Scene {

static Domain::Vec3d lerp(const Domain::Vec3d& a, const Domain::Vec3d& b, double t) { return (1.0 - t) * a + t * b; }

CameraTargetAnimation::CameraTargetAnimation(CameraTrackballController& trackball, const Domain::Vec3d& start_position,
    const Domain::Vec3d& final_position, double duration)
    : AbstractAnimation(duration)
    , m_trackball(trackball), m_start_position(start_position), m_final_position(final_position)
{
    set_state(Platform::AnimationState::Ready);
}

void CameraTargetAnimation::on_start()
{
    m_trackball.set_pivot(m_final_position);
}

void CameraTargetAnimation::on_update(double t)
{
    m_trackball.set_target(lerp(m_start_position, m_final_position, t));
}

} // namespace Slic3r::App::Scene
