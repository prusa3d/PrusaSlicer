#pragma once

#include <atomic>

namespace Slic3r {

/**
 * @brief Thread-safe ID sequence generator
 * @tparam T ID data type
 */
template <typename T = size_t>
class IdGenerator {
public:
    IdGenerator() = default;
    IdGenerator(const IdGenerator<T>&) = delete;
    IdGenerator(IdGenerator<T>&& other)  noexcept : m_next_id(other.m_next_id.load()) {}

    /**
     * @brief Creates new ID seqeuence generator
     * @param first_id ID the sequence should start with
     */
    explicit IdGenerator(T first_id =  T(0)): m_next_id(first_id) {}

    /**
     * @brief Gets next ID from sequence in thread-safe manner.
     * @return Next ID
     */
    T next_id() { return m_next_id.fetch_add(1, std::memory_order_relaxed); }

    /**
     * @brief Gets next ID from sequence in thread-safe manner.
     * @return Next ID
     */
    T operator()() { return next_id(); }

private:
    std::atomic<T> m_next_id;
};

}
