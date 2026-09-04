#pragma once

#include <boost/signals2.hpp>

namespace Slic3r::Biz {

/**
 * \brief Base class for Observable-like objects providing signal for subscription
 * \tparam Handler Notification handler function type
 */
template<typename Handler> class Notifiable
{
public:
    using Signal = boost::signals2::signal<Handler>;
    using SignalConnection = boost::signals2::connection;

    SignalConnection subscribe(const typename Signal::slot_type &subscriber) {
        return m_signal.connect(subscriber);
    }

protected:
    Signal m_signal;
};

/**
 * \brief Abstract read-only observable.
 * Subclass is expected to:
 * - provide modification and/or modification tracking API, and
 * - call `notify` method after the value is changed to notify registered observers.
 * \tparam T Type of value being observed
 */
template<typename T> class Observable : public Notifiable<void(const T &)>
{
public:
    Observable() = default;
    virtual ~Observable() = default;

    /**
     * \brief Get the current value the observable is tracking the modifications of.
     * \return Value being observed
     */
    [[nodiscard]] virtual const T &get() const = 0;

protected:
    /**
     * \brief Notify registered observers that the value was changed
     */
    void notify() const { this->m_signal(get()); }
};

template<typename T> class ObservableValue : public Observable<T>
{
public:
    struct ModificationScope
    {
        explicit ModificationScope(ObservableValue<T> &parent) : m_parent(parent) {}
        ~ModificationScope() {
            if (m_modified) {
                m_parent.notify();
            }
        }

        T &value() {
            m_modified = true;
            return m_parent.m_value;
        }

    private:
        ObservableValue<T> &m_parent;
        bool m_modified{false};
    };

    ObservableValue() = default;
    explicit ObservableValue(const T &value) : m_value(value) {}

    // prevent copying
    ObservableValue(ObservableValue &) = delete;
    ObservableValue &operator=(ObservableValue &) = delete;

    [[nodiscard]] const T &get() const override { return m_value; }

    void set(const T &value) {
        if (m_value != value) {
            m_value = value;
            this->notify();
        }
    }

    template<typename R> R track_change(std::function<R(T *)> modifier) {
        T val = m_value;
        const R ret = modifier(&val);
        const bool modified = val != m_value;
        if (modified) {
            set(val);
        }
        return ret;
    }

    template<> void track_change<void>(std::function<void(T *)> modifier) {
        T val = m_value;
        modifier(&val);
        const bool modified = val != m_value;
        if (modified) {
            set(val);
        }
    }
    ModificationScope transaction() { return ModificationScope(*this); }

    ObservableValue<T> &operator=(const T &value) {
        set(value);
        return *this;
    }

    explicit operator const T &() const { return get(); }

protected:
    T m_value;
};

} // namespace Slic3r::Biz
