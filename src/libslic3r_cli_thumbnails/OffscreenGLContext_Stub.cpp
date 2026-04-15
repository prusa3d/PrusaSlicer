///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer (stub fallback)
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Used on platforms for which no real offscreen backend is compiled in.
// `create()` always returns nullptr and the CLI proceeds with no thumbnails,
// preserving current behaviour. Guard must be the strict complement of the
// platform backends — see OffscreenGLContext_EGL.cpp for the Linux/BSD list.
#if !defined(__APPLE__) && !defined(_WIN32) \
 && !defined(__linux__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) \
 && !defined(__NetBSD__) && !defined(__DragonFly__)

#include "OffscreenGLContext.hpp"

namespace Slic3r {
namespace CLIThumbnails {

std::unique_ptr<OffscreenGLContext>
OffscreenGLContext::create(int /*width*/, int /*height*/, std::string *error_out)
{
    if (error_out)
        *error_out = "No offscreen GL backend built for this platform; CLI thumbnails disabled.";
    return nullptr;
}

} // namespace CLIThumbnails
} // namespace Slic3r

#endif
