///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer (macOS/CGL)
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifdef __APPLE__

#include "OffscreenGLContext.hpp"

#include <boost/log/trivial.hpp>

// OpenGL on macOS has been marked deprecated since 10.14. It still ships in
// macOS 26 and is what PrusaSlicer's GUI uses via wxGLContext. We silence the
// deprecation warnings here so the compile stays clean.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

namespace Slic3r {
namespace CLIThumbnails {
namespace {

class OffscreenGLContextCGL final : public OffscreenGLContext
{
public:
    OffscreenGLContextCGL(int w, int h, CGLContextObj ctx)
        : OffscreenGLContext(w, h), m_ctx(ctx) {}

    ~OffscreenGLContextCGL() override
    {
        if (m_ctx) {
            if (::CGLGetCurrentContext() == m_ctx)
                ::CGLSetCurrentContext(nullptr);
            ::CGLDestroyContext(m_ctx);
        }
    }

    bool make_current() override
    {
        if (!m_ctx) return false;
        const CGLError err = ::CGLSetCurrentContext(m_ctx);
        if (err != kCGLNoError) {
            BOOST_LOG_TRIVIAL(error) << "CGLSetCurrentContext failed: "
                                     << ::CGLErrorString(err);
            return false;
        }
        return true;
    }

    void release() override
    {
        ::CGLSetCurrentContext(nullptr);
    }

    const char *backend_name() const override { return "CGL"; }

private:
    CGLContextObj m_ctx = nullptr;
};

} // namespace

std::unique_ptr<OffscreenGLContext>
OffscreenGLContext::create(int width, int height, std::string *error_out)
{
    // FBOs don't require a drawable, so we don't ask for PBuffer attributes
    // (which are deprecated on macOS since 10.7). We just need a context with
    // accelerated rendering and reasonable color/depth/stencil sizes.
    const CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAColorSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize,   (CGLPixelFormatAttribute)8,
        kCGLPFADepthSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
        (CGLPixelFormatAttribute)0,
    };

    CGLPixelFormatObj pix = nullptr;
    GLint num_virtual_screens = 0;
    CGLError err = ::CGLChoosePixelFormat(attrs, &pix, &num_virtual_screens);
    if (err != kCGLNoError || pix == nullptr) {
        const char *msg = ::CGLErrorString(err);
        if (error_out) *error_out = std::string("CGLChoosePixelFormat: ") + (msg ? msg : "unknown");
        return nullptr;
    }

    CGLContextObj ctx = nullptr;
    err = ::CGLCreateContext(pix, nullptr, &ctx);
    ::CGLDestroyPixelFormat(pix);
    if (err != kCGLNoError || ctx == nullptr) {
        const char *msg = ::CGLErrorString(err);
        if (error_out) *error_out = std::string("CGLCreateContext: ") + (msg ? msg : "unknown");
        return nullptr;
    }

    return std::make_unique<OffscreenGLContextCGL>(width, height, ctx);
}

} // namespace CLIThumbnails
} // namespace Slic3r

#endif // __APPLE__
