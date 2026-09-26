///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer
///|/ Portions inspired by OrcaSlicer's CLI thumbnail support (AGPL-3.0,
///|/ Copyright (c) SoftFever and Bambu Lab).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#pragma once

#include <memory>
#include <string>

namespace Slic3r::App::CLI::Thumbnails {

/**
 * @brief Native offscreen context for CLI thumbnail rendering.
 *
 * The backend is selected at build time: CGL on macOS, WGL on Windows,
 * and EGL (surfaceless with a pbuffer fallback) on Linux and BSD.
 * Context creation failures return nullptr and an optional error message.
 */
class OffscreenGLContext
{
public:
    static std::unique_ptr<OffscreenGLContext>
    create(int width, int height, std::string* error_out = nullptr);

    virtual ~OffscreenGLContext() = default;

    virtual bool make_current()              = 0;
    virtual void release()                   = 0;
    virtual const char* backend_name() const = 0;

    int width() const
    {
        return m_width;
    }

    int height() const
    {
        return m_height;
    }

protected:
    OffscreenGLContext(int w, int h) : m_width(w), m_height(h) {}

    int m_width  = 0;
    int m_height = 0;

private:
    OffscreenGLContext(const OffscreenGLContext&)            = delete;
    OffscreenGLContext& operator=(const OffscreenGLContext&) = delete;
};

} // namespace Slic3r::App::CLI::Thumbnails
