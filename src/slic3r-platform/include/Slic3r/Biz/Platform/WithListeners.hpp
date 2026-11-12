#pragma once

#include "Slic3r/Biz/Platform/ListenerList.hpp"

/** @class WithListeners
 * @brief Subclass from WithListeners<A, B, C> to make your class have add_listener<L>
 * and remove_listener<L> as part of its public API for each L in {A, B, C}.
 * You can invoke the listerns from within your class using invoke_listeners<L>.
 *
 * Note that you can only have single set of listeners per type, meaning
 * that WithListeners<A, A> is invalid.
 *
 * ### Example Usage:
 * @code
 * class MyInteractor : public WithListeners<IChangedListener, IDeletedListener>
 * {
 *     void something_happended() {
 *         this->invoke_listeners<IChangedListener>([](...){...});
 *     }
 * };
 *
 * ChangedListener listener; # Implements IChangedListener.
 * MyInteractor interactor;
 * interactor.add_listener<IChangedListener>(&listener);
 * interactor.something_happened(); # listener get notified.
 * @endcode */
template <typename ...Args>
class WithListeners {
public:
    template<typename L>
    void add_listener(std::type_identity_t<L>* listener) {
        constexpr std::size_t I{WithListeners::index_in_tuple<L>()};
        std::get<I>(m_listener_lists).add(listener);
    }

    template<typename L>
    bool remove_listener(std::type_identity_t<L>* listener) {
        constexpr std::size_t I{WithListeners::index_in_tuple<L>()};
        return std::get<I>(m_listener_lists).remove(listener);
    }

protected:
    template <typename L>
    void invoke_listeners(std::function<void(L*)> func)
    {
        constexpr std::size_t I{WithListeners::index_in_tuple<L>()};
        std::get<I>(m_listener_lists).invoke(func);
    }

private:
    using Tuple = std::tuple<Slic3r::Biz::ListenerList<Args>...>;
protected:
    Tuple m_listener_lists;
private:
    template <typename L, std::size_t I = 0>
    static constexpr std::size_t index_in_tuple() {
        static_assert(I < std::tuple_size<Tuple>::value, "The listener list is not in the tuple!");

        using Listener = typename std::remove_reference_t<std::tuple_element_t<I, Tuple>>::ListenerType;
        if constexpr(std::is_base_of_v<Listener, L>) {
            return I;
        } else {
            return index_in_tuple<L, I + 1>();
        }
    }
};

// Same as WithListeners but allow register only one listener per type(interface IListener)
template <typename ...Args>
class WithListener {
public:
    template<typename L>
    void set_listener(std::type_identity_t<L>* listener) {
        std::get<L*>(m_listeners) = listener;
    }
    template<typename L>
    bool unset_listener(std::type_identity_t<L>* listener = nullptr) {
        if (std::get<L*>(m_listeners) == nullptr) return false; // already removed
        std::get<L*>(m_listeners) = nullptr;
        return true;
    }
protected:
    template<typename L>
    void invoke_listener(std::function<void(L*)>&& func) {
        L* listener_ptr = std::get<L*>(m_listeners);
        if (listener_ptr) {
            func(listener_ptr);
        }
    }
private:
    std::tuple<Args*...> m_listeners;
};
