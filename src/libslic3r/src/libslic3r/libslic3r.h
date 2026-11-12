#ifndef _libslic3r_h_
#define _libslic3r_h_

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Math.hpp"

#define GCODEVIEWER_APP_NAME "PrusaSlicer G-code Viewer"
#define GCODEVIEWER_APP_KEY  "PrusaSlicerGcodeViewer"

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <memory>
#include <array>
#include <algorithm>
#include <ostream>
#include <iostream>
#include <math.h>
#include <queue>
#include <sstream>
#include <cstdio>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
#include <cassert>
#include <cmath>
#include <type_traits>
#include <optional>

#ifdef _WIN32
// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
// https://github.com/microsoft/STL/issues/147#issuecomment-1090148740
// Thus it is recommended to use boost::container::deque instead.
#include <boost/container/deque.hpp>
#endif // _WIN32

#include "Technologies.hpp"
#include "Slic3r/Semver.hpp"

using coord_t = 
#if 1
// Saves around 32% RAM after slicing step, 6.7% after G-code export (tested on PrusaSlicer 2.2.0 final).
    Slic3r::Domain::coord_t;
#else
    //FIXME At least FillRectilinear2 and std::boost Voronoi require coord_t to be 32bit.
    int64_t;
#endif

using Slic3r::Domain::EPSILON;
// Scaling factor for a conversion from coord_t to double: 10e-6
// This scaling generates a following fixed point representation with for a 32bit integer:
// 0..4294mm with 1nm resolution
// int32_t fits an interval of (-2147.48mm, +2147.48mm)
// with int64_t we don't have to worry anymore about the size of the int.
static constexpr double SCALING_FACTOR = 0.000001;
static constexpr double PI = 3.141592653589793238;
// When extruding a closed loop, the loop is interrupted and shortened a bit to reduce the seam.
static constexpr double LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER = 0.15;
// Maximum perimeter length for the loop to apply the small perimeter speed. 
#define                 SMALL_PERIMETER_LENGTH  ((6.5 / SCALING_FACTOR) * 2 * PI)
static constexpr double INSET_OVERLAP_TOLERANCE = 0.4;
// 3mm ring around the top / bottom / bridging areas.
//FIXME This is quite a lot.
static constexpr double EXTERNAL_INFILL_MARGIN = 3.;

constexpr auto SCALED_EPSILON{scale_(Slic3r::Domain::EPSILON)};

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif /* UNUSED */

// Write slices as SVG images into out directory during the 2D processing of the slices.
// #define SLIC3R_DEBUG_SLICE_PROCESSING

namespace Slic3r {

extern Semver SEMVER;

// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
template<class T, class Allocator = std::allocator<T>>
using deque = 
#ifdef _WIN32
    // Use boost implementation, which allocates blocks of 512 bytes instead of blocks of 8 bytes.
    boost::container::deque<T, Allocator>;
#else // _WIN32
    std::deque<T, Allocator>;
#endif // _WIN32

template<typename T, typename Q>
inline T unscale(Q v) { return T(v) * T(SCALING_FACTOR); }

// Temporary measure.
using Domain::Axis;

using Domain::X;
using Domain::Y;
using Domain::Z;
using Domain::E;
using Domain::F;
using Domain::NUM_AXES;
using Domain::UNKNOWN_AXIS;
using Domain::NUM_AXES_WITH_UNKNOWN;

template<class T, class... Args> // Arbitrary allocator can be used
void clear_and_shrink(std::vector<T, Args...>& vec)
{
    // shrink_to_fit does not garantee the release of memory nor does it clear()
    std::vector<T, Args...> tmp;
    vec.swap(tmp);
    assert(vec.capacity() == 0);
}

// Append the source in reverse.
template <typename T>
inline void append_reversed(std::vector<T>& dest, const std::vector<T>& src)
{
    if (dest.empty()) 
        dest = {src.rbegin(), src.rend()};
    else
        dest.insert(dest.end(), src.rbegin(), src.rend());
}

// Append the source in reverse.
template <typename T>
inline void append_reversed(std::vector<T>& dest, std::vector<T>&& src)
{
    if (dest.empty())
        dest = {std::make_move_iterator(src.rbegin),
                std::make_move_iterator(src.rend)};
    else
        dest.insert(dest.end(), 
            std::make_move_iterator(src.rbegin()),
            std::make_move_iterator(src.rend()));
    // Release memory of the source contour now.
    src.clear();
    src.shrink_to_fit();
}

// Casting an std::vector<> from one type to another type without warnings about a loss of accuracy.
template<typename T_TO, typename T_FROM>
std::vector<T_TO> cast(const std::vector<T_FROM> &src) 
{
    std::vector<T_TO> dst;
    dst.reserve(src.size());
    for (const T_FROM &a : src)
        dst.emplace_back((T_TO)a);
    return dst;
}

template <typename T>
inline void remove_nulls(std::vector<T*> &vec)
{
	vec.erase(
    	std::remove_if(vec.begin(), vec.end(), [](const T *ptr) { return ptr == nullptr; }),
    	vec.end());
}

// Variant of std::lower_bound() with compare predicate, but without the key.
// This variant is very useful in case that the T type is large or it does not even have a public constructor.
template<class ForwardIt, class LowerThanKeyPredicate>
ForwardIt lower_bound_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_than_key)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);
 
    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (lower_than_key(*it)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class T, class Compare=std::less<>>
ForwardIt binary_find(ForwardIt first, ForwardIt last, const T& value, Compare comp={})
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced 
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare. 
    // This is stricter than lower_bound requirement (see above)
 
    first = std::lower_bound(first, last, value, comp);
    return first != last && !comp(value, *first) ? first : last;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class LowerThanKeyPredicate, class EqualToKeyPredicate>
ForwardIt binary_find_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_thank_key, EqualToKeyPredicate equal_to_key)
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced 
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare. 
    // This is stricter than lower_bound requirement (see above)
 
    first = lower_bound_by_predicate(first, last, lower_thank_key);
    return first != last && equal_to_key(*first) ? first : last;
}

template<typename ContainerType, typename ValueType> inline bool contains(const ContainerType &c, const ValueType &v)
    { return std::find(c.begin(), c.end(), v) != c.end(); }
template<typename T> inline bool contains(const std::initializer_list<T> &il, const T &v)
    { return std::find(il.begin(), il.end(), v) != il.end(); }

template<typename ContainerType, typename ValueType> inline bool one_of(const ValueType &v, const ContainerType &c)
    { return contains(c, v); }
template<typename T> inline bool one_of(const T& v, const std::initializer_list<T>& il)
    { return contains(il, v); }

using Slic3r::sqr;

template <typename T, typename Number>
constexpr inline T lerp(const T& a, const T& b, Number t)
{
    assert((t >= Number(-EPSILON)) && (t <= Number(1) + Number(EPSILON)));
    return (Number(1) - t) * a + t * b;
}

template <typename Number>
[[deprecated("Use Slic3r::Domain::fuzzy_compare")]]
constexpr inline bool is_approx(Number value, Number test_value, Number precision = EPSILON)
{
    return std::fabs(double(value) - double(test_value)) < double(precision);
}

template<typename Number>
constexpr inline bool is_approx(const std::optional<Number> &value,
                                const std::optional<Number> &test_value)
{
    return (!value.has_value() && !test_value.has_value()) ||
        (value.has_value() && test_value.has_value() && is_approx<Number>(*value, *test_value));
}

// A meta-predicate which is true for integers wider than or equal to coord_t
template<class I> struct is_scaled_coord
{
    static const constexpr bool value =
        std::is_integral<I>::value &&
        std::numeric_limits<I>::digits >=
            std::numeric_limits<coord_t>::digits;
};

// Meta predicates for floating, 'scaled coord' and generic arithmetic types
// Can be used to restrict templates to work for only the specified set of types.
// parameter T is the type we want to restrict
// parameter O (Optional defaults to T) is the type that the whole expression
// will be evaluated to.
// e.g. template<class T> FloatingOnly<T, bool> is_nan(T val);
// The whole template will be defined only for floating point types and the
// return type will be bool.
// For more info how to use, see docs for std::enable_if
//
template<class T, class O = T> 
using FloatingOnly = std::enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class O = T>
using ScaledCoordOnly = std::enable_if_t<is_scaled_coord<T>::value, O>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

template<class T, class O = T>
using ArithmeticOnly = std::enable_if_t<std::is_arithmetic<T>::value, O>;

template<class T, class I, class... Args> // Arbitrary allocator can be used
IntegerOnly<I, std::vector<T, Args...>> reserve_vector(I capacity)
{
    std::vector<T, Args...> ret;
    if (capacity > I(0))
        ret.reserve(size_t(capacity));

    return ret;
}

namespace detail_strip_ref_wrappers {
template<class T> struct StripCVRef_ { using type = std::remove_cvref_t<T>; };
template<class T> struct StripCVRef_<std::reference_wrapper<T>>
{
    using type = std::remove_cv_t<T>;
};
} // namespace detail

// Removes reference wrappers as well
template<class T> using  StripCVRef =
    typename detail_strip_ref_wrappers::StripCVRef_<std::remove_cvref_t<T>>::type;

template<class IntType = int, class = IntegerOnly<IntType, void>>
class IntIterator {
    IntType m_val;
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = IntType;
    using pointer           = IntType*;  // or also value_type*
    using reference         = IntType&;  // or also value_type&

    IntIterator(IntType v): m_val{v} {}

    IntIterator & operator++() { ++m_val; return *this; }
    IntIterator operator++(int) { auto cpy = *this; ++m_val; return cpy; }
    IntIterator & operator--() { --m_val; return *this; }
    IntIterator operator--(int) { auto cpy = *this; --m_val; return cpy; }

    IntType operator*() const { return m_val; }
    IntType operator->() const { return m_val; }

    bool operator==(const IntIterator& other) const
    {
        return m_val == other.m_val;
    }

    bool operator!=(const IntIterator& other) const
    {
        return !(*this == other);
    }
};

template<class IntType, class = IntegerOnly<IntType>>
auto range(IntType from, IntType to)
{
    return Range{IntIterator{from}, IntIterator{to}};
}

template<class T, class = FloatingOnly<T>>
constexpr T NaN = std::numeric_limits<T>::quiet_NaN();

constexpr float NaNf = NaN<float>;
constexpr double NaNd = NaN<double>;

// Rounding up.
// 1.5 is rounded to 2
// 1.49 is rounded to 1
// 0.5 is rounded to 1,
// 0.49 is rounded to 0
// -0.5 is rounded to 0,
// -0.51 is rounded to -1,
// -1.5 is rounded to -1.
// -1.51 is rounded to -2.
// If input is not a valid float (it is infinity NaN or if it does not fit)
// the float to int conversion produces a max int on Intel and +-max int on ARM.
template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

// Helper to be used in static_assert.
template<class T> struct always_false { enum { value = false }; };

// Map a generic function to each argument following the mapping function
template<class Fn, class...Args>
Fn for_each_argument(Fn &&fn, Args&&...args)
{
    // see https://www.fluentcpp.com/2019/03/05/for_each_arg-applying-a-function-to-each-argument-of-a-function-in-cpp/
    (fn(std::forward<Args>(args)),...);

    return fn;
}

// Call fn on each element of the input tuple tup.
template<class Fn, class Tup>
Fn for_each_in_tuple(Fn fn, Tup &&tup)
{
    auto mpfn = [&fn](auto&...pack) {
        for_each_argument(fn, pack...);
    };

    std::apply(mpfn, tup);

    return fn;
}

} // namespace Slic3r

#endif // _libslic3r_h_
