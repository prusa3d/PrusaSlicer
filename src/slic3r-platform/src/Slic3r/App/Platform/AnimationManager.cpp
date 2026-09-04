#include "Slic3r/App/Platform/AnimationManager.hpp"

#include <algorithm>

namespace Slic3r::App::Platform {

bool AnimationManager::update()
{
    for (auto it = m_animations.begin(); it != m_animations.end(); /*no update here*/) {
        AbstractAnimation& anim = **it;
        switch (anim.state())
        {
        case AnimationState::Ready:
        {
            anim.start();
            break;
        }
        case AnimationState::Running:
        {
            anim.update();
            break;
        }
        case AnimationState::Completed:
        case AnimationState::Stopped:
        {
            it = m_animations.erase(it);
            continue;
        }
        default: { break; }
        }
        ++it;
    }
    return is_running();
}

bool AnimationManager::is_running() const
{
    return std::ranges::any_of(
        m_animations,
        [](const auto& a)
        {
            if (a == nullptr) {
                return false;
            }
            const auto state = a->state();
            return state == AnimationState::Running || state == AnimationState::Ready;
        }
    );
}

AnimationState AnimationManager::state(const AbstractAnimation* anim) const
{
    if (anim == nullptr)
        return AnimationState::Undefined;

    auto it = std::find_if(m_animations.begin(), m_animations.end(), [&](const auto& a) {
        return a.get() == anim;
    });
    return (it != m_animations.end()) ? (*it)->state() : AnimationState::Undefined;
}

void AnimationManager::terminate_all()
{
    for (auto& anim : m_animations) {
        anim->terminate();
    }
    m_animations.clear();
}

} // namespace Slic3r::App::Platform