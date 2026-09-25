///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "X11ErrorTrap.hpp"

#ifdef __WXGTK__

#include <cassert>
#include <cstdio>

#include <wx/utils.h>

// Keep the Xlib header last: it defines macros (None, Bool, Status, Success, True, False, ...)
// that would break the headers included after it.
#include <X11/Xlib.h>

namespace Slic3r {
namespace GUI {

namespace {

// Written by the Xlib error handler, which is a plain function without a user data pointer.
struct RecordedError
{
    bool          recorded{ false };
    unsigned char error_code{ 0 };
    unsigned char request_code{ 0 };
    unsigned char minor_code{ 0 };
};

RecordedError   s_recorded_error;
::XErrorHandler s_previous_handler{ nullptr };
bool            s_installed{ false };

int record_x_error(::Display * /* display */, ::XErrorEvent *event)
{
    // Keep the first error, the following ones are usually its consequences.
    if (!s_recorded_error.recorded) {
        s_recorded_error.recorded     = true;
        s_recorded_error.error_code   = event->error_code;
        s_recorded_error.request_code = event->request_code;
        s_recorded_error.minor_code   = event->minor_code;
    }
    // Xlib ignores the return value.
    return 0;
}

} // namespace

X11ErrorTrap::X11ErrorTrap()
{
    // The display wxGLContext works with: wxGetX11Display() in wxWidgets' src/unix/glx11.cpp
    // returns wxGetDisplayInfo().dpy, both on GTK2 and GTK3.
    const wxDisplayInfo info = wxGetDisplayInfo();
    if (info.type != wxDisplayX11 || info.dpy == nullptr)
        return;
    assert(!s_installed);
    if (s_installed)
        return;

    ::Display *display = static_cast<::Display *>(info.dpy);
    // Deliver the errors of the requests issued before the trap to the handler that was responsible
    // for them, so that everything recorded below was caused inside the trap.
    ::XSync(display, False);
    s_recorded_error   = RecordedError();
    s_previous_handler = ::XSetErrorHandler(&record_x_error);
    s_installed        = true;
    m_display          = display;
}

X11ErrorTrap::~X11ErrorTrap()
{
    if (m_display == nullptr)
        return;
    ::XSync(static_cast<::Display *>(m_display), False);
    ::XSetErrorHandler(s_previous_handler);
    s_previous_handler = nullptr;
    s_installed        = false;
}

bool X11ErrorTrap::sync_and_check()
{
    if (m_display == nullptr)
        return false;
    ::Display *display = static_cast<::Display *>(m_display);
    ::XSync(display, False);
    if (s_recorded_error.recorded && m_error.empty()) {
        char text[256] = "";
        ::XGetErrorText(display, s_recorded_error.error_code, text, int(sizeof(text)));
        // Same numbers as in GTK's "X Window System error" message.
        char codes[96];
        std::snprintf(codes, sizeof(codes), " (error_code %d request_code %d minor_code %d)",
                      int(s_recorded_error.error_code), int(s_recorded_error.request_code),
                      int(s_recorded_error.minor_code));
        m_error = std::string(text) + codes;
    }
    return s_recorded_error.recorded;
}

} // namespace GUI
} // namespace Slic3r

#endif // __WXGTK__
