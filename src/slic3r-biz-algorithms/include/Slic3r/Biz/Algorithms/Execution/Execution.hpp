
#pragma once

#include <type_traits>
#include <utility>
#include <cstddef>
#include <iterator>
#include <algorithm>

namespace Slic3r::Biz::Algorithms::Execution {

// Override for valid execution policies
template<class EP>
struct IsExecutionPolicy_ : public std::false_type
{};

template<class EP>
constexpr bool IsExecutionPolicy = IsExecutionPolicy_<std::remove_cvref_t<EP>>::value;

template<class EP, class T = void>
using ExecutionPolicyOnly = std::enable_if_t<IsExecutionPolicy<EP>, T>;

template<class T, class O = T>
using IteratorOnly = std::enable_if_t<
    !std::is_same_v<typename std::iterator_traits<T>::value_type, void>, O
>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

// This struct needs to be specialized for each execution policy.
// See ExecutionSeq.hpp and ExecutionTBB.hpp for example.
template<class EP, class En = void>
struct Traits
{};

template<class EP>
using AsTraits = Traits<std::remove_cvref_t<EP>>;

// Each execution policy should declare two types of mutexes. A a spin lock and
// a blocking mutex. These types should satisfy the BasicLockable concept.
template<class EP>
using SpinningMutex = typename AsTraits<EP>::SpinningMutex;
template<class EP>
using BlockingMutex = typename AsTraits<EP>::BlockingMutex;

// Query the available threads for concurrency.
template<class EP, class = ExecutionPolicyOnly<EP>>
size_t max_concurrency(const EP& ep)
{
    return AsTraits<EP>::max_concurrency(ep);
}

// foreach loop with the execution policy passed as argument. Granularity can
// be specified explicitly. max_concurrency() can be used for optimal results.
template<class EP, class It, class Fn, class = ExecutionPolicyOnly<EP>>
void for_each(const EP& ep, It from, It to, Fn&& fn, size_t granularity = 1)
{
    AsTraits<EP>::for_each(ep, from, to, std::forward<Fn>(fn), std::max(granularity, size_t(1)));
}

// A reduce operation with the execution policy passed as argument.
// mergefn has T(const T&, const T&) signature
// accessfn has T(I) signature if I is an integral type and
// T(const I::value_type &) if I is an iterator type.
template<class EP, class I, class MergeFn, class T, class AccessFn, class = ExecutionPolicyOnly<EP>>
T reduce(
    const EP& ep,
    I from,
    I to,
    const T& init,
    MergeFn&& mergefn,
    AccessFn&& accessfn,
    size_t granularity = 1
)
{
    return AsTraits<EP>::reduce(
        ep, from, to, init, std::forward<MergeFn>(mergefn), std::forward<AccessFn>(accessfn),
        std::max(granularity, size_t(1))
    );
}

// An overload of reduce method to be used with iterators as 'from' and 'to'
// arguments. Access functor is omitted here.
template<class EP, class I, class MergeFn, class T, class = ExecutionPolicyOnly<EP>>
T reduce(const EP& ep, I from, I to, const T& init, MergeFn&& mergefn, size_t granularity = 1)
{
    return reduce(
        ep, from, to, init, std::forward<MergeFn>(mergefn), [](const auto& i) { return i; },
        std::max(granularity, size_t(1))
    );
}
} // namespace Slic3r::Biz::Algorithms::Execution
