#pragma once

#include <chrono>
#include <thread>

namespace Tests {

/**
 * @brief Dispatches the main thread queue until the predicate holds or the timeout expires.
 * @return false on the timeout.
 */
template <typename Dispatcher, typename Predicate>
bool dispatch_until(
    Dispatcher& dispatcher,
    Predicate predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(10)
)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        dispatcher.dispatch_enqueued();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

/**
 * @brief Dispatches the main thread queue for the whole duration.
 */
template <typename Dispatcher>
void dispatch_for(Dispatcher& dispatcher, std::chrono::milliseconds duration)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
        dispatcher.dispatch_enqueued();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace Tests
