#pragma once

#include <chrono>

namespace Slic3r::App::Platform {

enum class AnimationState
{
    Undefined,
    Ready,
    Running,
    Stopped,
    Paused,
    Completed
};

class AbstractAnimation
{
public:
    explicit AbstractAnimation(double duration_in_sec);
    virtual ~AbstractAnimation() = default;

    AnimationState state() const { return m_state; }
    double duration() const { return m_duration; }

    void start();
    void update();
    void stop();
    void pause();
    void resume();
    void terminate();

protected:
    void set_state(AnimationState state) { m_state = state; }

    virtual void on_start() = 0;
    virtual void on_update(double t) = 0;
    virtual void on_stop() {}
    virtual void on_pause() {}
    virtual void on_resume() {}
    virtual void on_terminate() {}

private:
    AnimationState m_state{ AnimationState::Undefined };
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time;
    // duration in seconds
    double m_duration{ 0.0 };
};

} // namespace Slic3r::App::Platform
