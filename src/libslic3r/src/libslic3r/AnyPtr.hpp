#ifndef ANYPTR_HPP
#define ANYPTR_HPP

#include <memory>
#include <type_traits>
#include <boost/variant.hpp>

namespace Slic3r {

// A general purpose pointer holder that can hold any type of smart pointer
// or raw pointer which can own or not own any object they point to.
// In case a raw pointer is stored, it is not destructed so ownership is
// assumed to be foreign.
//
// The stored pointer is not checked for being null when dereferenced.
//
// This is a movable only object due to the fact that it can possibly hold
// a unique_ptr which can only be moved.
//
// Drawbacks:
// No custom deleters are supported when storing a unique_ptr, but overloading
// std::default_delete for a particular type could be a workaround
//
// raw array types are problematic, since std::default_delete also does not
// support them well.
template<class T>
class AnyPtr {
    enum { RawPtr, UPtr, ShPtr };

    boost::variant<T*, std::unique_ptr<T>, std::shared_ptr<T>> ptr;

    template<class Self> static T *get_ptr(Self &&s)
    {
        switch (s.ptr.which()) {
        case RawPtr: return boost::get<T *>(s.ptr);
        case UPtr: return boost::get<std::unique_ptr<T>>(s.ptr).get();
        case ShPtr: return boost::get<std::shared_ptr<T>>(s.ptr).get();
        }

        return nullptr;
    }

    template<class TT> friend class AnyPtr;

    template<class TT>
    using SimilarPtrOnly = std::enable_if_t<std::is_convertible_v<TT*, T*>>;

public:

    AnyPtr() noexcept = default;

    AnyPtr(T *p) noexcept: ptr{p} {}

    AnyPtr(std::nullptr_t) noexcept {};

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr(TT *p) noexcept : ptr{p}
    {}
    template<class TT = T, class = SimilarPtrOnly<TT>>
    AnyPtr(std::unique_ptr<TT> p) noexcept : ptr{std::unique_ptr<T>(std::move(p))}
    {}
    template<class TT = T, class = SimilarPtrOnly<TT>>
    AnyPtr(std::shared_ptr<TT> p) noexcept : ptr{std::shared_ptr<T>(std::move(p))}
    {}

    AnyPtr(AnyPtr &&other) noexcept : ptr{std::move(other.ptr)} {}

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr(AnyPtr<TT> &&other) noexcept
    {
        this->operator=(std::move(other));
    }

    AnyPtr(const AnyPtr &other) = delete;

    AnyPtr &operator=(AnyPtr &&other) noexcept
    {
        ptr = std::move(other.ptr);
        return *this;
    }

    AnyPtr &operator=(const AnyPtr &other) = delete;

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr& operator=(AnyPtr<TT> &&other) noexcept
    {
        switch (other.ptr.which()) {
        case RawPtr: *this = boost::get<TT *>(other.ptr); break;
        case UPtr: *this = std::move(boost::get<std::unique_ptr<TT>>(other.ptr)); break;
        case ShPtr: *this = std::move(boost::get<std::shared_ptr<TT>>(other.ptr)); break;
        }

        return *this;
    }

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr &operator=(TT *p) noexcept
    {
        ptr = static_cast<T *>(p);
        return *this;
    }

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr &operator=(std::unique_ptr<TT> p) noexcept
    {
        ptr = std::unique_ptr<T>(std::move(p));
        return *this;
    }

    template<class TT, class = SimilarPtrOnly<TT>>
    AnyPtr &operator=(std::shared_ptr<TT> p) noexcept
    {
        ptr = std::shared_ptr<T>(std::move(p));
        return *this;
    }

    const T &operator*() const noexcept { return *get_ptr(*this); }
    T &operator*() noexcept { return *get_ptr(*this); }

    T *operator->() noexcept { return get_ptr(*this); }
    const T *operator->() const noexcept { return get_ptr(*this); }

    T *get() noexcept { return get_ptr(*this); }
    const T *get() const noexcept { return get_ptr(*this); }

    operator bool() const noexcept
    {
        switch (ptr.which()) {
        case RawPtr: return bool(boost::get<T *>(ptr));
        case UPtr: return bool(boost::get<std::unique_ptr<T>>(ptr));
        case ShPtr: return bool(boost::get<std::shared_ptr<T>>(ptr));
        }

        return false;
    }

    // If the stored pointer is a shared pointer, returns a reference
    // counted copy. Empty shared pointer is returned otherwise.
    std::shared_ptr<T> get_shared_cpy() const noexcept
    {
        std::shared_ptr<T> ret;

        if (ptr.which() == ShPtr)
            ret = boost::get<std::shared_ptr<T>>(ptr);

        return ret;
    }

    // If the underlying pointer is unique, convert to shared pointer
    void convert_unique_to_shared() noexcept
    {
        if (ptr.which() == UPtr)
            ptr = std::shared_ptr<T>{std::move(boost::get<std::unique_ptr<T>>(ptr))};
    }

    // Returns true if the data is owned by this AnyPtr instance
    bool is_owned() const noexcept
    {
        return ptr.which() == UPtr || ptr.which() == ShPtr;
    }
};


} // namespace Slic3r

#endif // ANYPTR_HPP
