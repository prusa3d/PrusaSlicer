///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer (Windows/WGL)
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#if defined(_WIN32)

#include "OffscreenGLContext.hpp"

#include <boost/log/trivial.hpp>

// Minimize Win32 header surface; we only need GDI + OpenGL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

// WGL_ARB_create_context and WGL_ARB_pixel_format constants. We pull only what
// we need to avoid a full `wglext.h` dependency.
#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB            0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB            0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB             0x9126
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB         0x00000001
#endif

namespace Slic3r {
namespace CLIThumbnails {
namespace {

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC (WINAPI *)(HDC, HGLRC, const int *);

class OffscreenGLContextWGL final : public OffscreenGLContext
{
public:
    OffscreenGLContextWGL(int w, int h, HWND hwnd, HDC hdc, HGLRC hglrc, ATOM atom, HINSTANCE hinst)
        : OffscreenGLContext(w, h), m_hwnd(hwnd), m_hdc(hdc), m_hglrc(hglrc), m_atom(atom), m_hinst(hinst) {}

    ~OffscreenGLContextWGL() override
    {
        if (m_hglrc) {
            if (wglGetCurrentContext() == m_hglrc)
                wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_hglrc);
        }
        if (m_hdc && m_hwnd)
            ReleaseDC(m_hwnd, m_hdc);
        if (m_hwnd)
            DestroyWindow(m_hwnd);
        if (m_atom && m_hinst)
            UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(m_atom)), m_hinst);
    }

    bool make_current() override
    {
        return wglMakeCurrent(m_hdc, m_hglrc) == TRUE;
    }

    void release() override
    {
        wglMakeCurrent(nullptr, nullptr);
    }

    const char *backend_name() const override { return "WGL"; }

private:
    HWND      m_hwnd  = nullptr;
    HDC       m_hdc   = nullptr;
    HGLRC     m_hglrc = nullptr;
    ATOM      m_atom  = 0;
    HINSTANCE m_hinst = nullptr;
};

} // namespace

std::unique_ptr<OffscreenGLContext>
OffscreenGLContext::create(int width, int height, std::string *error_out)
{
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSW wc = {};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hinst;
    wc.lpszClassName = L"PrusaSlicerCLIThumbWnd";
    const ATOM atom = RegisterClassW(&wc);
    if (!atom) {
        if (error_out) *error_out = "RegisterClassW failed";
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP,
                                0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
    if (!hwnd) {
        if (error_out) *error_out = "CreateWindowExW failed";
        UnregisterClassW(wc.lpszClassName, hinst);
        return nullptr;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        if (error_out) *error_out = "GetDC failed";
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hinst);
        return nullptr;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    const int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        if (error_out) *error_out = "ChoosePixelFormat/SetPixelFormat failed";
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hinst);
        return nullptr;
    }

    // Bootstrap a legacy context so we can query wglCreateContextAttribsARB.
    HGLRC legacy = wglCreateContext(hdc);
    if (!legacy) {
        if (error_out) *error_out = "wglCreateContext (legacy) failed";
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hinst);
        return nullptr;
    }
    wglMakeCurrent(hdc, legacy);

    auto wgl_create_context_attribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    HGLRC final_ctx = nullptr;
    if (wgl_create_context_attribs) {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 2,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
            0
        };
        final_ctx = wgl_create_context_attribs(hdc, nullptr, attribs);
    }

    if (final_ctx) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(legacy);
    } else {
        // Fall back to the legacy context. Most drivers give at least 2.1 here,
        // which is below PrusaSlicer's minimum (3.2), but we try anyway and let
        // the renderer detect that and skip gracefully.
        final_ctx = legacy;
    }

    wglMakeCurrent(hdc, final_ctx);
    (void)width; (void)height;  // window size irrelevant — we render to FBOs.

    return std::make_unique<OffscreenGLContextWGL>(width, height, hwnd, hdc, final_ctx, atom, hinst);
}

} // namespace CLIThumbnails
} // namespace Slic3r

#endif // _WIN32
