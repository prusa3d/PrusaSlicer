#include "Slic3r/App/Platform/AbstractAnimation.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Math.hpp"

#include <algorithm>

namespace Slic3r::App::Platform {

AbstractAnimation::AbstractAnimation(double duration_in_sec)
    : m_duration(duration_in_sec)
{
    DEBUG_ASSERT(m_duration > 0.0);
}

void AbstractAnimation::start()
{
    m_start_time = std::chrono::high_resolution_clock::now();
    if (m_state == AnimationState::Ready) {
        m_state = AnimationState::Running;
        on_start();
    }
}

void AbstractAnimation::update()
{
    if (m_state == AnimationState::Running) {
        double delta_time =
            0.001 * double(duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_start_time).count());
        double t = smoothstep(std::clamp(delta_time / m_duration, 0.0, 1.0));
        on_update(t);
        if (t == 1.0)
            set_state(Platform::AnimationState::Completed);
    }
}

void AbstractAnimation::stop()
{
    if (m_state == AnimationState::Running) {
        m_state = AnimationState::Stopped;
        on_stop();
    }
}

void AbstractAnimation::pause()
{
    if (m_state == AnimationState::Running) {
        m_state = AnimationState::Paused;
        on_pause();
    }
}

void AbstractAnimation::resume()
{
    if (m_state == AnimationState::Paused) {
        m_state = AnimationState::Running;
        on_resume();
    }
}

void AbstractAnimation::terminate()
{
    if (m_state == AnimationState::Running) {
        on_update(1.0);
        m_state = AnimationState::Completed;
        on_terminate();
    }
}

} // namespace Slic3r::App::Platform
