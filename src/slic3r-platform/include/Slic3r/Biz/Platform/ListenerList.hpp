#pragma once

#include <list>
#include <functional>
#include <algorithm>
#include <vector>
#include "Slic3r/Utils.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz {

template <class L>
class ListenerList
{
public:
    using ListenerType = L;

    bool add(L* listener)
    {
        if (listener == nullptr
            || std::find(m_listeners.begin(), m_listeners.end(), listener) != m_listeners.end())
        {
            return false; // already exist in listeners
        }
        if (m_invoke_in_progress) {
            if (contains(listener, m_listeners_to_add)) {
                return false;
            }
            insert(listener, m_listeners_to_add);
        } else {
            m_listeners.push_back(listener);
        }
        return true;
    }

    bool remove(L* listener)
    {
        const bool pending_add{contains(listener, m_listeners_to_add)};
        if (pending_add) {
            erase(listener, m_listeners_to_add);
        }
        auto it = std::find(m_listeners.begin(), m_listeners.end(), listener);
        if (it == m_listeners.end()) {
            return pending_add;
        }
        if (m_invoke_in_progress) {
            insert(listener, m_listeners_to_remove);
        } else {
            m_listeners.erase(it);
        }
        return true;
    }

    void invoke(std::function<void(L*)> func)
    {
        m_invoke_in_progress = true;
        const ScopeGuard scope_guard{[&]() { m_invoke_in_progress = false; }};
        for (auto it = m_listeners.begin(); it != m_listeners.end();) {
            auto current{*it++};
            if (contains(current, m_listeners_to_remove)) {
                continue;
            }
            func(current);
        }
        m_invoke_in_progress = false;

        for (L* listener : m_listeners_to_remove) {
            ASSERT(remove(listener));
        }
        m_listeners_to_remove = {};

        for (L* listener : m_listeners_to_add) {
            const bool added{add(listener)};
            if (added) {
                func(listener);
            }
        }
        m_listeners_to_add = {};
    }

private:
    using Listeners = std::list<L*>;
    Listeners m_listeners;

    bool m_invoke_in_progress{false};

    // Maintain order.
    std::vector<L*> m_listeners_to_remove;
    std::vector<L*> m_listeners_to_add;

    static void insert(L* listener, std::vector<L*>& listeners)
    {
        ASSERT(listeners.size() < 1000, "Over 1000 listeners can be a performance issue!");
        const auto it{std::ranges::find(listeners, listener)};
        if (it != listeners.end()) {
            return;
        }
        listeners.push_back(listener);
    }

    static bool contains(L* listener, const std::vector<L*>& listeners)
    {
        ASSERT(listeners.size() < 1000, "Over 1000 listeners can be a performance issue!");
        const auto it{std::ranges::find(listeners, listener)};
        return it != listeners.end();
    }

    static void erase(L* listener, std::vector<L*>& listeners)
    {
        ASSERT(listeners.size() < 1000, "Over 1000 listeners can be a performance issue!");
        const auto it{std::ranges::find(listeners, listener)};
        if (it == listeners.end()) {
            return;
        }
        listeners.erase(it);
    }
};
} // namespace Slic3r::Biz
