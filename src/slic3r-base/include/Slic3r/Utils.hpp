#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {

template<typename T, typename Alloc, typename Alloc2>
inline void append(std::vector<T, Alloc>& dest, const std::vector<T, Alloc2>& src)
{
    if (dest.empty()) {
        dest = src; // Copy
    } else {
        dest.insert(dest.end(), src.begin(), src.end());
    }
}

template<typename T, typename Alloc>
inline void append(std::vector<T, Alloc>& dest, std::vector<T, Alloc>&& src)
{
    if (dest.empty()) {
        dest = std::move(src);
    } else {
        dest.insert(dest.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));

        // Release memory of the source contour now.
        src.clear();
        src.shrink_to_fit();
    }
}

template <typename T>
inline void sort_remove_duplicates(std::vector<T>& vec)
{
    std::sort(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

// Compute the next highest power of 2 of 32-bit v
// http://graphics.stanford.edu/~seander/bithacks.html
inline uint16_t next_highest_power_of_2(uint16_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    return ++ v;
}
inline uint32_t next_highest_power_of_2(uint32_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++ v;
}
inline uint64_t next_highest_power_of_2(uint64_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return ++ v;
}

// On some implementations (such as some versions of clang), the size_t is a type of its own, so we need to overload for size_t.
// Typically, though, the size_t type aliases to uint64_t / uint32_t.
// We distinguish that here and provide implementation for size_t if and only if it is a distinct type
template<class T> size_t next_highest_power_of_2(T v,
    typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0,     // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,  // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,  // T is not uint32_t
    typename std::enable_if<sizeof(T) == 8, T>::type = 0)                     // T is 64 bits
{
    return next_highest_power_of_2(uint64_t(v));
}
template<class T> size_t next_highest_power_of_2(T v,
    typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0,     // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,  // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,  // T is not uint32_t
    typename std::enable_if<sizeof(T) == 4, T>::type = 0)                     // T is 32 bits
{
    return next_highest_power_of_2(uint32_t(v));
}

// A very simple range concept implementation with iterator-like objects.
// This should be replaced by std::ranges::subrange (C++20)
template<class It> class Range
{
    It from, to;
public:

    // The class is ready for range based for loops.
    It begin() const { return from; }
    It end() const { return to; }

    // The iterator type can be obtained this way.
    using iterator = It;
    using value_type = typename std::iterator_traits<It>::value_type;

    Range() = default;
    Range(It b, It e) : from(std::move(b)), to(std::move(e)) {}

    // Some useful container-like methods...
    inline size_t size() const { return std::distance(from, to); }
    inline bool   empty() const { return from == to; }
};

template<class Cont> auto range(Cont &&cont)
{
    return Range{std::begin(cont), std::end(cont)};
}

template<class Cont> auto crange(Cont &&cont)
{
    return Range{std::cbegin(cont), std::cend(cont)};
}

template<typename INDEX_TYPE>
inline INDEX_TYPE prev_idx_modulo(INDEX_TYPE idx, const INDEX_TYPE count)
{
    if (idx == 0) {
        idx = count;
    }

    return --idx;
}

template<typename INDEX_TYPE>
inline INDEX_TYPE next_idx_modulo(INDEX_TYPE idx, const INDEX_TYPE count)
{
    if (++idx == count) {
        idx = 0;
    }

    return idx;
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::size_type prev_idx_modulo(typename CONTAINER_TYPE::size_type idx, const CONTAINER_TYPE& container)
{
    return prev_idx_modulo(idx, container.size());
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::size_type next_idx_modulo(typename CONTAINER_TYPE::size_type idx, const CONTAINER_TYPE& container)
{
    return next_idx_modulo(idx, container.size());
}

template<typename T>
inline bool is_in_range(const T& value, const T& low, const T& high)
{
    return low <= value && value <= high;
}

// Returns next utf8 sequence length. =number of bytes in string, that creates together one utf-8 character. 
// Starting at pos. ASCII characters returns 1. Works also if pos is in the middle of the sequence.
size_t get_utf8_sequence_length(const std::string& text, size_t pos = 0);
size_t get_utf8_sequence_length(const char *seq, size_t size);

// A very lightweight RAII wrapper around C FILE.
// The old C file API is much faster than C++ streams, thus they are recommended for processing large / huge files.
struct FilePtr {
    FilePtr(FILE *f) : f(f) {}
    ~FilePtr() { this->close(); }
    void close() { 
        if (this->f) {
            ::fclose(this->f);
            this->f = nullptr;
        }
    }
    FILE* f = nullptr;
};

class ScopeGuard
{
public:
    typedef std::function<void()> Closure;
    Closure closure;

public:
    ScopeGuard() {}
    ScopeGuard(Closure closure) : closure(std::move(closure)) {}
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard &&other) : closure(std::move(other.closure)) {}

    ~ScopeGuard()
    {
        if (closure) { closure(); }
    }

    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard &&other)
    {
        closure = std::move(other.closure);
        return *this;
    }

    void reset() { closure = Closure(); }
};

std::string string_printf(const char *format, ...);

// Standard "generated by Slic3r version xxx timestamp xxx" header string, 
// to be placed at the top of Slic3r generated files.
std::string header_slic3r_generated();

} // namespace Slic3r
