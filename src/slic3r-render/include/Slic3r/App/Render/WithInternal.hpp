#pragma once

#include <memory>

namespace Slic3r::App::Render {

class WithInternal {
public:
    struct Internal
    {
        virtual ~Internal() = default;
    protected:
        Internal() = default;
    };

    virtual ~WithInternal() noexcept = default;
    WithInternal(WithInternal&&) = default;
    WithInternal& operator=(WithInternal&&) = default;

    template<typename C> C& get_internal_as() { return *static_cast<C*>(m_internal.get()); }
    template<typename C> const C& get_internal_as() const
    {
        return *static_cast<const C*>(m_internal.get());
    }

protected:
    template <typename T>
    struct InternalType { using type = T; };
    template <typename C>
    explicit WithInternal(C* internal) : m_internal(internal) {}

    template <typename C>
    explicit WithInternal(InternalType<C>) : m_internal(std::make_unique<C>()) {}

    template <typename C, typename ...Args>
    explicit WithInternal(InternalType<C>, Args&&... args)
        : m_internal(std::make_unique<C>(std::forward<Args...>(args...)))
    {}

protected:
    std::unique_ptr<Internal> m_internal;
};

}