#pragma once
/**
 * Assertion API
 * - ASSERT(x[, message]),  ASSERT_VAL(x[, message]) enforces condition in both debug AND release configurations (!)
 * - DEBUG_ASSERT(x[, message]), DEBUG_ASSERT_VAL(x[, message]) enforces condition only in debug configuration.
 * - PANIC(message)
 * For more details see [libassert](https://github.com/jeremy-rifkin/libassert)
 */
#ifndef __EMSCRIPTEN__

#include "libassert/assert.hpp"

namespace Slic3r {
void init_assert();
}

#else // #ifndef __EMSCRIPTEN__

// As we cannot use libassert in emscripten follows minimal implementation of the libassert API:

#include <iostream>


#if !defined(__PRETTY_FUNCTION__) && !defined(__GNUC__)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#define __ASSERT_GET_3TH_ARG(arg1, arg2, arg3, ...) arg3
#define ASSERT(...) __ASSERT_GET_3TH_ARG(__VA_ARGS__, ASSERT_2, ASSERT_1) (__VA_ARGS__)

#define ASSERT_1(x) if (!(x)) {                                                         \
        std::cerr << "Assertion " << #x << " failed in " << __FILE__ << ":" << __LINE__ \
                  << " (function " << __PRETTY_FUNCTION__ << ")\n";                     \
        std::abort();                                                                   \
    }

#define ASSERT_2(x, message) if (!(x)) {                              \
        std::cerr << "Assertion " << #x << ": " << message << "\nin " \
                  << __FILE__ << ":" << __LINE__                      \
                  << " (function " << __PRETTY_FUNCTION__ << ")\n";   \
        std::abort();                                                 \
    }

#define ASSERT_VAL(...) __ASSERT_GET_3TH_ARG(__VA_ARGS__, ASSERT_VAL_2, ASSERT_VAL_1)(__VA_ARGS__)
#define ASSERT_VAL_1(x) ::Slic3r::assert_val(x, __FILE__, __LINE__, __PRETTY_FUNCTION__,  #x)
#define ASSERT_VAL_2(x, message) ::Slic3r::assert_val(x, __FILE__, __LINE__, __PRETTY_FUNCTION__,  #x, message)
#define UNREACHABLE(...) ASSERT(false, __VA_ARGS__)


namespace Slic3r {

template<typename T>
inline T assert_val(T expr, const char* filename, size_t line, const char* func, const char* expr_str, const char* message = nullptr)
{
    if (!(expr)) {
        std::cerr << "Assertion " << expr_str << " failed ";
        if (message)
            std::cerr << "with message '" << message << "'\n";
        std::cerr << "in " << filename << ":" << line
                  << " (function " << func << ")\n";
        std::abort();
    }
    return expr;
}

}

#define PANIC_0() {                                                 \
        std::cerr << "Panic! in " << __FILE__ << ":" << __LINE__    \
                  << " (function " << __PRETTY_FUNCTION__ << ")\n"; \
        std::abort();                                                                                          \
    }
#define PANIC_1(message) {                                          \
        std::cerr << "Panic! " << message << "\nin "                \
                  << __FILE__ << ":" << __LINE__                    \
                  << " (function " << __PRETTY_FUNCTION__ << ")\n"; \
        std::abort();                                               \
    }

#define __ASSERT_PANIC_FUNC_CHOOSER(_f1, _f2, ...) _f2
#define __ASSERT_FUNC_RECOMPOSER(argsWithParentheses) __ASSERT_PANIC_FUNC_CHOOSER argsWithParentheses
#define __ASSERT_PANIC_CHOOSE_FROM_ARG_COUNT(...) __ASSERT_FUNC_RECOMPOSER((__VA_ARGS__, PANIC_1, ))
#define __ASSERT_PANIC_NO_ARG_EXPANDER() ,PANIC_0
#define __ASSERT_PANIC_MACRO_CHOOSER(...) __ASSERT_PANIC_CHOOSE_FROM_ARG_COUNT(__ASSERT_PANIC_NO_ARG_EXPANDER __VA_ARGS__ ())
#define PANIC(...) __ASSERT_PANIC_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)


#ifdef NDEBUG

#define DEBUG_ASSERT(...)
#define DEBUG_ASSERT_VAL(x, ...) (x)

#else // #ifdef NDEBUG

#define DEBUG_ASSERT(...) ASSERT(__VA_ARGS__)
#define DEBUG_ASSERT_VAL(...) ASSERT_VAL(__VA_ARGS__)

#endif // #ifdef NDEBUG

#endif // #ifndef __EMSCRIPTEN__

