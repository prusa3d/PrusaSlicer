#pragma once

#include <atomic>
#include <vector>

namespace Slic3r::App::libvgcode {

// By default, types are not atomic,
template<typename T> auto constexpr is_atomic = false;

// but std::atomic<T> types are,
template<typename T> auto constexpr is_atomic<std::atomic<T>> = true;

template<typename T = unsigned long long>
struct BitSet
{
    BitSet() = default;
    BitSet(size_t size) : size(size), blocks(1 + (size / (sizeof(T) * 8))) { clear(); }

    void clear() {
        for (size_t i = 0; i < blocks.size(); ++i) {
            blocks[i] &= T(0);
        }
    }

    void setAll() {
        for (size_t i = 0; i < blocks.size(); ++i) {
            blocks[i] |= ~T(0);
        }
    }

    // return true if bit changed
    bool set(size_t index) {
        auto [block_idx, bit_idx] = coords(index);
        T mask = (T(1) << bit_idx);
        bool flip = mask ^ blocks[block_idx];
        blocks[block_idx] |= mask;
        return flip;
    }

    // return true if bit changed
    bool reset(size_t index) {
        auto [block_idx, bit_idx] = coords(index);
        T mask = (T(1) << bit_idx);
        bool flip = mask ^ blocks[block_idx];
        blocks[block_idx] &= (~mask);
        return flip;
    }

    bool operator [] (size_t index) const {
        auto [block_idx, bit_idx] = coords(index);
        return ((blocks[block_idx] >> bit_idx) & 1) != 0;
    }

    template<typename U>
    BitSet& operator &= (const BitSet<U>& other) {
        static_assert(sizeof(T) == sizeof(U), "Type1 and Type2 must be of the same size.");
        for (size_t i = 0; i < blocks.size(); ++i) {
            blocks[i] &= other.blocks[i];
        }
        return *this;
    }

    // Atomic set operation (enabled only for atomic types), return true if bit changed
    template<typename U = T>
    inline typename std::enable_if<is_atomic<U>, bool>::type set_atomic(size_t index) {
        auto [block_idx, bit_idx] = coords(index);
        T mask = static_cast<T>(1) << bit_idx;
        T oldval = blocks[block_idx].fetch_or(mask, std::memory_order_relaxed);
        return oldval ^ (oldval or mask);
    }

    // Atomic reset operation (enabled only for atomic types), return true if bit changed
    template<typename U = T>
    inline typename std::enable_if<is_atomic<U>, bool>::type reset_atomic(size_t index) {
        auto [block_idx, bit_idx] = coords(index);
        T mask = ~(static_cast<T>(1) << bit_idx);
        T oldval = blocks[block_idx].fetch_and(mask, std::memory_order_relaxed);
        return oldval ^ (oldval and mask);
    }

    std::pair<size_t, size_t> coords(size_t index) const {
        size_t block_idx = index / (sizeof(T) * 8);
        size_t bit_idx = index % (sizeof(T) * 8);
        return { block_idx, bit_idx };
    }

    size_t size{ 0 };
    std::vector<T> blocks{ 0 };
};

} // namespace Slic3r::App::libvgcode
