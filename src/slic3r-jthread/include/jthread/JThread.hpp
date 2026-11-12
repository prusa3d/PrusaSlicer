/**
Dumb implementation of some std::jthread functionality we need.
- many methods (e.g. the whole callback mechanism) are not implemented
- request stop does not return bool - it is difficult to
  implement correctly and we do not need it
*/

#ifndef slic3r_bsp_JThread_hpp_
#define slic3r_bsp_JThread_hpp_

#include <iostream>
#include <thread>
#include <atomic>

namespace Slic3r::Biz::JThread {

using StopSource = std::unique_ptr<std::atomic<bool>>;

class StopToken {
public:
    StopToken() = default;
    StopToken(StopSource& stop_requested): stop_requested_ptr{stop_requested.get()} {}

    [[nodiscard]] bool stop_requested() const {
        if (stop_requested_ptr == nullptr) {
            return false;
        }
        return stop_requested_ptr->load();
    }
private:
    std::atomic<bool>* stop_requested_ptr{nullptr};
};

class JThread {
public:
    JThread() = default;
    JThread(JThread&& other) noexcept = default;

    template<
        typename Callable,
        typename... Args,
        typename = std::enable_if_t<!std::is_same_v<std::remove_cvref_t<Callable>, JThread>>
    >
    explicit JThread(Callable&& f, Args&&... args):
    m_thread{
        create(m_stop_requested, std::forward<Callable>(f),
        std::forward<Args>(args)...)
    } {}

    ~JThread() {
        if (joinable()) {
            request_stop();
            join();
        }
    }

    JThread& operator=(const JThread&) = delete;
    JThread& operator=(JThread&& other) noexcept {
        JThread(std::move(other)).swap(*this); //Ensureses the original thread stops.
        return *this;
    };

    void swap(JThread& other) {
        std::swap(m_stop_requested, other.m_stop_requested);
        std::swap(m_thread, other.m_thread);
    }

    void request_stop() {
        m_stop_requested->store(true);
    }

    void join() {
        m_thread.join();
    }

    void detach() {
        m_thread.detach();
    }

    [[nodiscard]] bool joinable() const noexcept {
        return m_thread.joinable();
    }

private:
    template<typename Callable, typename... Args>
    static std::thread create(StopSource& stop_source, Callable&& f, Args&&... args) {
	    if constexpr(std::is_invocable_v<std::decay_t<Callable>, StopToken, std::decay_t<Args>...>) {
	        return std::thread{
                std::forward<Callable>(f),
                StopToken{stop_source},
			    std::forward<Args>(args)...
            };
        } else {
            static_assert(
                std::is_invocable_v<std::decay_t<Callable>, std::decay_t<Args>...>,
                "std::thread arguments must be invocable after"
                " conversion to rvalues"
            );

            return std::thread{std::forward<Callable>(f), std::forward<Args>(args)...};
	    }
    }

    StopSource m_stop_requested{std::make_unique<std::atomic<bool>>(false)};
    std::thread m_thread;
};

} // namespace Slic3r::Biz::BSP

#endif // slic3r_bsp_JThread_hpp_
