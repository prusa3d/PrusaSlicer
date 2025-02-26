#ifdef WIN32
    // Why?
    #define _WIN32_WINNT 0x0502
    // The standard Windows includes.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif // WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif // NOMINMAX
    #include <Windows.h>
    #include <wchar.h>
    #ifdef SLIC3R_GUI
    extern "C"
    {
        // Let the NVIDIA and AMD know we want to use their graphics card
        // on a dual graphics card system.
        __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
        __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
    }
    #endif /* SLIC3R_GUI */
#endif /* WIN32 */

#include <cstdio>
#include <string>
#include <cstring>
#include <iostream>
#include <vector>

#include <boost/nowide/args.hpp>

#include "libslic3r/libslic3r.h"

#include "PrusaSlicer.hpp"

// __has_feature() is used later for Clang, this is for compatibility with other compilers (such as GCC and MSVC)
#ifndef __has_feature
#   define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" {
    // Based on https://github.com/google/skia/blob/main/tools/LsanSuppressions.cpp
    const char *__lsan_default_suppressions() {
        return "leak:libfontconfig\n"           // FontConfig looks like it leaks, but it doesn't.
               "leak:libfreetype\n"             // Unsure, appeared upgrading Debian 9->10.
               "leak:libGLX_nvidia.so\n"        // For NVidia driver.
               "leak:libnvidia-glcore.so\n"     // For NVidia driver.
               "leak:libnvidia-tls.so\n"        // For NVidia driver.
               "leak:terminator_CreateDevice\n" // For Intel Vulkan drivers.
               "leak:swrast_dri.so\n"           // For Mesa 3D software driver.
               "leak:amdgpu_dri.so\n"           // For AMD driver.
               "leak:libdrm_amdgpu.so\n"        // For AMD driver.
               "leak:libdbus-1.so\n"            // For D-Bus library. Unsure if it is a leak or not.
            ;
    }
}
#endif

#if defined(SLIC3R_UBSAN)
extern "C" {
    // Enable printing stacktrace by default. It can be disabled by running PrusaSlicer with "UBSAN_OPTIONS=print_stacktrace=0".
    const char *__ubsan_default_options() {
        return "print_stacktrace=1";
    }
}
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
extern "C" {
    __declspec(dllexport) int __stdcall slic3r_main(int argc, wchar_t **argv)
    {
        // Convert wchar_t arguments to UTF8.
        std::vector<std::string> 	argv_narrow;
        std::vector<char*>			argv_ptrs(argc + 1, nullptr);
        for (size_t i = 0; i < argc; ++ i)
            argv_narrow.emplace_back(boost::nowide::narrow(argv[i]));
        for (size_t i = 0; i < argc; ++ i)
            argv_ptrs[i] = argv_narrow[i].data();
        // Call the UTF8 main.
        return Slic3r::CLI::run(argc, argv_ptrs.data());
    }
}
#else /* _MSC_VER */
int main(int argc, char **argv)
{
    return Slic3r::CLI::run(argc, argv);
}
#endif /* _MSC_VER */
