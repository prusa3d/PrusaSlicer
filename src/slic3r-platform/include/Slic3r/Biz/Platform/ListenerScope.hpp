#pragma once

namespace Slic3r::Biz {

/**
 * @brief The ListenerScope class is a RAII utility wrapper for add_listener and remove_listener
 * methods. It is intended to put this class as a class attribute.
 */
template<class Listener, class Publisher, class Observer>
class ListenerScope
{
public:
    ListenerScope(Publisher& publisher, Observer& observer)
        : m_publisher(&publisher), m_observer(&observer)
    {
        m_publisher->template add_listener<Listener>(m_observer);
    }
    virtual ~ListenerScope()
    {
        if (m_publisher)
            m_publisher->template remove_listener<Listener>(m_observer);
    }

    ListenerScope(const ListenerScope& rhs) = delete;
    ListenerScope& operator=(const ListenerScope& rhs) = delete;

    ListenerScope(ListenerScope&& rhs) noexcept
        : m_publisher(rhs.m_publisher), m_observer(rhs.m_observer)
    {
        rhs.m_publisher = nullptr;
        rhs.m_observer = nullptr;
    }
    ListenerScope& operator=(ListenerScope&& rhs) noexcept
    {
        if (this != &rhs) {
            if (m_publisher)
                m_publisher->template remove_listener<Listener>(m_observer);
            m_publisher = rhs.m_publisher;
            m_observer = rhs.m_observer;
            rhs.m_publisher = nullptr;
            rhs.m_observer = nullptr;
        }
        return *this;
    }

private:
    Publisher* m_publisher;
    Observer* m_observer;
};

// ListenerScope<IProjectChangedListener, m_project_interactor, this>;

} // namespace Slic3r::Biz
