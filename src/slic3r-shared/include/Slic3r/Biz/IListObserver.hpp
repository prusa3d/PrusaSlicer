#pragma once

#include "Slic3r/Assert.hpp"

#include <cstddef>

namespace Slic3r::Biz {

/**
 * @note both from and to indexes are INCLUSIVE
 * This means IndexRange that should affect only index 10
 * should have from and to set to 10
 */
struct IndexRange
{
    IndexRange() = default;

    IndexRange(size_t index) : IndexRange(index, index) {}

    IndexRange(size_t from, size_t to) : from(from), to(to)
    {
        ASSERT(from <= to);
    }

    bool is_valid() const
    {
        return from <= to;
    }

    size_t from{0};
    size_t to{0};
};

/**
 * @brief The WeakerPointer class
 * it essentially acts as a raw Pointer.
 * Optionally can get weak_ptr and if so the is_valid method can return false
 * if weak_ptr is already expired.
 */
template <class Data>
class WeakerPointer
{
public:
    WeakerPointer() {}

    WeakerPointer(Data* raw_ptr) : m_ptr(raw_ptr) {}

    WeakerPointer(const std::weak_ptr<Data>& weak_ptr) :
        m_weak_ptr(weak_ptr),
        m_ptr(m_weak_ptr->lock().get())
    {}

    inline bool operator==(Data* other) const
    {
        return get() == other;
    }

    inline bool operator!=(Data* other) const
    {
        return !(*this == other);
    }

    inline bool operator==(const WeakerPointer<Data>& other)
    {
        return get() == other.get();
    }

    inline bool operator!=(const WeakerPointer<Data>& other)
    {
        return !(*this == other);
    }

    Data* operator->() const
    {
        return get();
    }

    Data* get() const
    {
        return m_ptr;
    }

    /**
     * @return for raw pointer always true, expired property otherwise
     */
    bool is_valid() const
    {
        return m_weak_ptr.has_value() ? !m_weak_ptr.value().expired() : m_ptr != nullptr;
    }

private:
    std::optional<std::weak_ptr<Data>> m_weak_ptr;
    Data* m_ptr{nullptr};
};

/**
 * @brief The UnsharedPointer class
 * Is a special pointer that acts as a shared_ptr which
 * can only be shared as weak_ptr. This is only useful
 * as a guard so that we can be sure no more shared_ptrs of this ptr
 * are created.
 */
template <class Data>
class UnsharedPointer
{
public:
    UnsharedPointer() {}

    UnsharedPointer(std::shared_ptr<Data> ptr) : m_ptr(ptr) {}

    operator bool() const noexcept {
        return m_ptr != nullptr;
    }

    Data* operator->() const
    {
        return m_ptr.get();
    }

    std::weak_ptr<Data> get() const
    {
        return m_ptr;
    }

    void reset(std::shared_ptr<Data> ptr = nullptr) {
        m_ptr = ptr;
    }

private:
    std::shared_ptr<Data> m_ptr;
};

template <class Data>
class IListObserver
{
public:
    /**
     * @brief on_inserted - Data at index was inserted
     */
    virtual void on_inserted(const Data& data, size_t index) {}
    /**
     * @brief on_will_be_removed - all Data in range [IndexRange.from, IndexRange.to] will be removed
     */
    virtual void on_will_be_removed(const IndexRange& index_range) {}
    /**
     * @brief on_removed - all Data in range [IndexRange.from, IndexRange.to] were removed
     */
    virtual void on_removed(const IndexRange& index_range) {}
    /**
     * @brief on_updated - add Data in range [IndexRange.from, IndexRange.to] were updated
     */
    virtual void on_updated(const IndexRange& index_range) {}
    /**
     * @param new_size (optional) - size which will the model have after reset
     */
    virtual void on_will_be_reset(std::optional<size_t> new_size = std::nullopt) {}
    /**
     * @brief on_reset - all Data is invalid, reconstruct List completely
     */
    virtual void on_reset() {}
    /**
     * @brief on_moved - Data from index was moved to to index
     */
    virtual void on_moved(size_t from, size_t to) {}
};

} // namespace Slic3r::Biz
