///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer (Linux/BSD EGL)
///|/
///|/ Portions inspired by the approach in prusa3d/PrusaSlicer#15327 (EGL-only,
///|/ closed without merge). This implementation generalizes the approach to
///|/ also work truly headless via EGL_MESA_platform_surfaceless, which is the
///|/ modern Mesa replacement for the removed OSMesa API.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Complement of the Stub guard below. Must match the CMake rule that picks
// OffscreenGLContext_EGL.cpp on UNIX-but-not-Apple — i.e. Linux plus the
// supported BSDs. Explicitly enumerating rather than using __unix__ because
// __unix__ is also defined by Apple clang on macOS.
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
 || defined(__NetBSD__) || defined(__DragonFly__)

#include "OffscreenGLContext.hpp"

#include <boost/log/trivial.hpp>
#include <cstring>
#include <string>

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace Slic3r {
namespace CLIThumbnails {
namespace {

// Returns true if `needle` appears as a space-separated token in the
// extensions string `haystack` (the EGL/GL extension-list convention).
bool has_token(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    const size_t n = std::strlen(needle);
    for (const char *p = haystack; *p; ) {
        while (*p == ' ') ++p;
        const char *start = p;
        while (*p && *p != ' ') ++p;
        if (static_cast<size_t>(p - start) == n && std::memcmp(start, needle, n) == 0)
            return true;
    }
    return false;
}

class OffscreenGLContextEGL final : public OffscreenGLContext
{
public:
    OffscreenGLContextEGL(int w, int h, EGLDisplay dpy, EGLContext ctx, EGLSurface surf)
        : OffscreenGLContext(w, h), m_dpy(dpy), m_ctx(ctx), m_surf(surf) {}

    ~OffscreenGLContextEGL() override
    {
        if (m_dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(m_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_surf != EGL_NO_SURFACE)
                eglDestroySurface(m_dpy, m_surf);
            if (m_ctx != EGL_NO_CONTEXT)
                eglDestroyContext(m_dpy, m_ctx);
            eglTerminate(m_dpy);
        }
    }

    bool make_current() override
    {
        return eglMakeCurrent(m_dpy, m_surf, m_surf, m_ctx) == EGL_TRUE;
    }

    void release() override
    {
        eglMakeCurrent(m_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    const char *backend_name() const override
    {
        return m_surf == EGL_NO_SURFACE ? "EGL(surfaceless)" : "EGL(pbuffer)";
    }

private:
    EGLDisplay m_dpy  = EGL_NO_DISPLAY;
    EGLContext m_ctx  = EGL_NO_CONTEXT;
    EGLSurface m_surf = EGL_NO_SURFACE;
};

// Try to open a surfaceless EGL display. Returns EGL_NO_DISPLAY if the
// platform isn't advertised or the required entry point isn't reachable.
//
// Tries two entry points in order:
//   1. eglGetPlatformDisplay   — EGL 1.5 core (preferred)
//   2. eglGetPlatformDisplayEXT — EGL_EXT_platform_base extension
// Some stacks only ship the EXT variant (e.g. older Mesa or EGL-on-GLVND
// without full 1.5 client), so we try both. The surfaceless platform is
// identified by the same enum in both cases; only the attrib-list element
// type differs (EGLAttrib vs EGLint), which is irrelevant when we pass
// nullptr.
//
// EGL_PLATFORM_SURFACELESS_MESA == 0x31DD. Defined in eglext.h on Mesa
// builds that ship the extension; we fall back to a literal if the header
// is older.
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
EGLDisplay try_surfaceless_display()
{
    const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!has_token(client_exts, "EGL_MESA_platform_surfaceless"))
        return EGL_NO_DISPLAY;

    using PFN_core = EGLDisplay (EGLAPIENTRY *)(EGLenum, void *, const EGLAttrib *);
    using PFN_ext  = EGLDisplay (EGLAPIENTRY *)(EGLenum, void *, const EGLint *);

    if (auto core = reinterpret_cast<PFN_core>(eglGetProcAddress("eglGetPlatformDisplay"))) {
        EGLDisplay dpy = core(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (dpy != EGL_NO_DISPLAY)
            return dpy;
    }
    if (auto ext = reinterpret_cast<PFN_ext>(eglGetProcAddress("eglGetPlatformDisplayEXT"))) {
        return ext(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }
    return EGL_NO_DISPLAY;
}

// Try to stand up an EGL context on the given display. Returns null on any
// failure, cleaning up whatever was allocated. `dpy` is eglTerminate'd on
// failure so the caller can try another display afterward without leaking.
std::unique_ptr<OffscreenGLContext>
try_initialize(EGLDisplay dpy, bool surfaceless, int width, int height, std::string *err)
{
    if (dpy == EGL_NO_DISPLAY) {
        if (err) *err = "no display";
        return nullptr;
    }

    EGLint major = 0, minor = 0;
    if (eglInitialize(dpy, &major, &minor) != EGL_TRUE) {
        if (err) *err = "eglInitialize failed";
        return nullptr;
    }

    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
        if (err) *err = "eglBindAPI(EGL_OPENGL_API) failed — desktop GL not supported by this EGL";
        eglTerminate(dpy);
        return nullptr;
    }

    // Ask for a pbuffer-renderable config even in surfaceless mode so we get
    // sane colour/depth/stencil formats to attach to our own FBO.
    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint    num_cfgs = 0;
    if (eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &num_cfgs) != EGL_TRUE || num_cfgs == 0) {
        if (err) *err = "eglChooseConfig: no matching EGLConfig";
        eglTerminate(dpy);
        return nullptr;
    }

    const EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        // Retry with a core profile in case compat isn't supported.
        const EGLint core_attrs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 2,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, core_attrs);
    }
    if (ctx == EGL_NO_CONTEXT) {
        if (err) *err = "eglCreateContext failed for GL 3.2";
        eglTerminate(dpy);
        return nullptr;
    }

    EGLSurface surf = EGL_NO_SURFACE;
    if (!surfaceless) {
        const EGLint pb_attrs[] = {
            EGL_WIDTH,  width  > 0 ? width  : 1,
            EGL_HEIGHT, height > 0 ? height : 1,
            EGL_NONE
        };
        surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
        if (surf == EGL_NO_SURFACE) {
            if (err) *err = "eglCreatePbufferSurface failed";
            eglDestroyContext(dpy, ctx);
            eglTerminate(dpy);
            return nullptr;
        }
    }

    // Validate that the context can actually be made current. On some drivers,
    // creating a surfaceless display + context succeeds but the first
    // eglMakeCurrent(surfaceless) fails — without this check the failure would
    // surface in the renderer, by which time the factory has no way to retry
    // with the default display + pbuffer path. Probing now lets create() do
    // the real fallback.
    if (eglMakeCurrent(dpy, surf, surf, ctx) != EGL_TRUE) {
        if (err) *err = "eglMakeCurrent failed on this display/context";
        if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        return nullptr;
    }
    // Release again so the caller controls when the context goes current.
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    BOOST_LOG_TRIVIAL(info) << "EGL offscreen context ready ("
                            << (surfaceless ? "surfaceless" : "pbuffer")
                            << "), EGL " << major << "." << minor;
    return std::make_unique<OffscreenGLContextEGL>(width, height, dpy, ctx, surf);
}

} // namespace

std::unique_ptr<OffscreenGLContext>
OffscreenGLContext::create(int width, int height, std::string *error_out)
{
    // Prefer the surfaceless platform on systems that advertise it (modern
    // Mesa on headless hosts). If it fails for any reason after returning a
    // display — e.g. the driver can't produce a desktop GL 3.2 context on the
    // surfaceless display — fall through to the normal EGL_DEFAULT_DISPLAY
    // path with a pbuffer. This is the portability guarantee the PR claims.
    std::string err_surfaceless;
    if (EGLDisplay sd = try_surfaceless_display(); sd != EGL_NO_DISPLAY) {
        if (auto ctx = try_initialize(sd, /*surfaceless=*/true, width, height, &err_surfaceless))
            return ctx;
        BOOST_LOG_TRIVIAL(info)
            << "EGL surfaceless path unavailable (" << err_surfaceless
            << "); falling back to default display.";
    }

    std::string err_default;
    if (auto ctx = try_initialize(eglGetDisplay(EGL_DEFAULT_DISPLAY),
                                  /*surfaceless=*/false, width, height, &err_default))
        return ctx;

    if (error_out) {
        *error_out = "EGL surfaceless: " + err_surfaceless +
                     "; EGL default: " + err_default;
    }
    return nullptr;
}

} // namespace CLIThumbnails
} // namespace Slic3r

#endif // __linux__ && !__APPLE__
