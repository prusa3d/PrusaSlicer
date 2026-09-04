/////|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral

// This is LEGACY version of libslic3r.h, stripped down to bits needed for
// loading legacy profiles.

#pragma once



namespace Slic3rLegacy {

using coord_t = 
#if 1
    int32_t;
#else
    int64_t;
#endif

using coordf_t = double;
static constexpr double EPSILON = 1e-4;
static constexpr double SCALING_FACTOR = 0.000001;

#ifndef scale_legacy_
#define scale_legacy_(val) ((val) / SCALING_FACTOR)
#endif

template<typename T>
constexpr inline T sqr(T x)
{
    return x * x;
}

template <typename Number>
constexpr inline bool is_approx(Number value, Number test_value, Number precision = EPSILON)
{
    return std::fabs(double(value) - double(test_value)) < double(precision);
}


template<class I> struct is_scaled_coord
{
    static const constexpr bool value =
        std::is_integral<I>::value &&
        std::numeric_limits<I>::digits >=
            std::numeric_limits<coord_t>::digits;
};

template<class T, class O = T> 
using FloatingOnly = std::enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class O = T>
using ScaledCoordOnly = std::enable_if_t<is_scaled_coord<T>::value, O>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

template<class T, class O = T>
using ArithmeticOnly = std::enable_if_t<std::is_arithmetic<T>::value, O>;

template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

// Helper to be used in static_assert.
template<class T> struct always_false { enum { value = false }; };

} // namespace Slic3rLegacy
