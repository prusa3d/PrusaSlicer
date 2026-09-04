#pragma once

#include "Slic3r/App/Platform/AbstractAnimation.hpp"

#include <vector>
#include <memory>

namespace Slic3r::App::Platform {

class AnimationManager
{
public:
    template<typename A, typename... ArgsT>
    A& add_animation(ArgsT&&... args)
    {
        m_animations.emplace_back(std::make_unique<A>(args...));
        auto& ptr = m_animations.back();
        return *static_cast<A*>(ptr.get());
    }

    /** Perform animations update.
     * @return true if any animation is in state AnimationState::Running.
     * @note Animations whose state is AnimationState::Completed and have no loop and
     *       animations whose state is AnimationState::Stopped are removed from the manager.
     *       If you keep pointers to such animations, use the state() method to check that they are still valid.
     *
     */
    bool update();

    bool is_running() const;

    /** Return the state of the given animation.
     * @param anim The animation whose state is required.
     *
     * @return the state of the given animation.
     * @note Return AnimationState::Undefined if the animation completed/stopped and it was removed.
     */
    AnimationState state(const AbstractAnimation* anim) const;

    /** Terminates all running animations.
     */
    void terminate_all();

private:
    std::vector<std::unique_ptr<AbstractAnimation>> m_animations;
};

} // namespace Slic3r::App::Platform
