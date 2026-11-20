#pragma once

#include <memory>

namespace Slic3r::Biz {

/**
 * @brief Lets callbacks that outlive their owner find out that it is gone.
 *
 * A member of the owner, declared after everything its callbacks use. A callback captures
 * token() and does nothing once the token has expired, which happens when the owner is
 * destroyed or calls end(). The check is meaningful on the thread that destroys the owner; a
 * callback on another thread has to hand over to that thread first.
 */
class Lifetime
{
public:
    using Token = std::weak_ptr<void>;

    Lifetime() = default;

    Lifetime(const Lifetime&)            = delete;
    Lifetime& operator=(const Lifetime&) = delete;

    /**
     * @brief Safe to call from any thread, also while the owner ends its lifetime.
     */
    Token token() const
    {
        return m_token;
    }

    /**
     * @brief Expires the tokens before the owner is destroyed, e.g. first thing in a destructor
     * that still has members its callbacks use.
     */
    void end()
    {
        m_alive.reset();
    }

private:
    std::shared_ptr<void> m_alive{std::make_shared<char>(0)};
    const Token m_token{m_alive};
};

} // namespace Slic3r::Biz
