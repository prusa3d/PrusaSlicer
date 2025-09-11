///|/ Copyright (c) Prusa Research 2021 - 2022 Tomáš Mészáros @tamasmeszaros, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_LocalesUtils_hpp_
#define slic3r_LocalesUtils_hpp_

#include <string>
#include <clocale>
#include <iomanip>
#include <cassert>
#include <string_view>

#ifdef __APPLE__
#include <xlocale.h>
#endif

namespace Slic3r {

// RAII wrapper that sets LC_NUMERIC to "C" on construction
// and restores the old value on destruction.
class CNumericLocalesSetter {
public:
    CNumericLocalesSetter();
    ~CNumericLocalesSetter();

private:
#ifdef _WIN32
    std::string m_orig_numeric_locale;
#else
    locale_t m_original_locale;
    locale_t m_new_locale;
#endif

};

// A function to check that current C locale uses decimal point as a separator.
// Intended mostly for asserts.
bool is_decimal_separator_point();


// A substitute for std::to_string that works according to
// C++ locales, not C locale. Meant to be used when we need
// to be sure that decimal point is used as a separator.
// (We use user C locales and "C" C++ locales in most of the code.)
std::string float_to_string_decimal_point(double value, int precision = -1);
//std::string float_to_string_decimal_point(float value,  int precision = -1);
double string_to_double_decimal_point(const std::string_view str, size_t* pos = nullptr);
float  string_to_float_decimal_point (const std::string_view str, size_t* pos = nullptr);

// Set locales to "C".
inline void set_c_locales()
{
#ifdef _WIN32
    _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    std::setlocale(LC_ALL, "C");
#else
    // We are leaking some memory here, because the newlocale() produced memory will never be released.
    // This is not a problem though, as there will be a maximum one worker thread created per physical thread.
    uselocale(newlocale(
#ifdef __APPLE__
        LC_ALL_MASK
#else // some Unix / Linux / BSD
        LC_ALL
#endif
        , "C", nullptr));
#endif
}

} // namespace Slic3r

#endif // slic3r_LocalesUtils_hpp_
