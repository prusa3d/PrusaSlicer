///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer
///|/ Portions inspired by OrcaSlicer's CLI thumbnail support (AGPL-3.0,
///|/ Copyright (c) SoftFever and Bambu Lab).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_cli_OffscreenGLContext_hpp_
#define slic3r_cli_OffscreenGLContext_hpp_

#include <memory>
#include <string>

namespace Slic3r {
namespace CLIThumbnails {

// Platform-neutral offscreen OpenGL context for use by the CLI thumbnail
// renderer. The concrete backend is chosen at runtime by `create()`:
//   Linux   -> EGL (EGL_MESA_platform_surfaceless, then PBuffer fallback)
//   macOS   -> CGL (no drawable)
//   Windows -> WGL (transient hidden window for context, then destroyed)
//
// Contract: `create()` returns nullptr on any failure, writing a human-readable
// reason into `error_out` if provided. Callers log and proceed without
// thumbnails; never throw.
class OffscreenGLContext
{
public:
    static std::unique_ptr<OffscreenGLContext> create(int width, int height,
                                                      std::string *error_out = nullptr);

    virtual ~OffscreenGLContext() = default;

    virtual bool        make_current() = 0;
    virtual void        release()      = 0;
    virtual const char* backend_name() const = 0;

    int width()  const { return m_width; }
    int height() const { return m_height; }

protected:
    OffscreenGLContext(int w, int h) : m_width(w), m_height(h) {}
    int m_width  = 0;
    int m_height = 0;

private:
    OffscreenGLContext(const OffscreenGLContext&) = delete;
    OffscreenGLContext& operator=(const OffscreenGLContext&) = delete;
};

} // namespace CLIThumbnails
} // namespace Slic3r

#endif // slic3r_cli_OffscreenGLContext_hpp_
