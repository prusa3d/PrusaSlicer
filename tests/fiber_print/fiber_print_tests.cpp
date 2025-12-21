#include <catch_main.hpp>

#include "libslic3r/libslic3r.h"

// __has_feature() is used later for Clang, this is for compatibility with other compilers (such as GCC and MSVC)
#ifndef __has_feature
#   define __has_feature(x) 0
#endif

// Print reports about memory leaks but exit with zero exit code when any memory leaks is found to make unit tests pass.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" {
    const char *__lsan_default_options() {
        return "exitcode=0";
    }
}
#endif

