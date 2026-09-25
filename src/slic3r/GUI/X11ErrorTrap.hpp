///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_X11ErrorTrap_hpp_
#define slic3r_GUI_X11ErrorTrap_hpp_

#ifdef __WXGTK__

#include <string>

namespace Slic3r {
namespace GUI {

// Linux/GTK only. While an X11ErrorTrap is alive, X11 protocol errors are recorded instead of being
// handed to the Xlib error handler installed before it. GTK installs a handler that prints
// "The program received an X Window System error" and calls _exit(1) for any error it did not
// expect, which is how the application died when the X server refused to create an OpenGL (GLX)
// context (GH #60). wxGLContext guards only a part of its constructor: the temporary legacy
// context it creates first (glXCreateContext, GLX minor opcode 3) and its destructor run under
// GTK's handler.
//
// This is the pattern wxWidgets' own wxGLContext uses around the rest of the context creation
// (src/unix/glx11.cpp): XSetErrorHandler(own handler), the GLX calls, XSync(), restore the
// previous handler. Errors of requests issued before the trap are flushed to the previous handler
// first, so every error the trap records was caused inside it. XSync() also delivers the errors
// Mesa queues without reporting them (glXCreateContext returns NULL, the BadValue stays pending).
//
// Does nothing if the display is not an X11 one. Xlib error handlers are process global: do not
// nest instances, and only use them on the GUI thread.
// The Xlib headers are kept out of this header on purpose, their macros (None, Bool, Status,
// Success, ...) clash with names used elsewhere in PrusaSlicer.
//
// To reproduce the failure (Linux, Mesa, Xvfb refuses indirect GLX contexts by default):
//   SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt LIBGL_ALWAYS_INDIRECT=1 \
//       xvfb-run -a -s '-screen 0 1920x1080x24' prusa-slicer
// or the same with `prusa-slicer --gcodeviewer` (SSL_CERT_FILE only avoids the modal certificate
// store prompt of the editor). Without the trap GTK reports "BadValue ... request_code ... (GLX)
// minor_code 3" and exits, with it the error is logged and explained in a message box.
class X11ErrorTrap
{
public:
    X11ErrorTrap();
    // Synchronizes with the X server, so that the errors of all requests issued inside the trap
    // are recorded, then restores the previous error handler.
    ~X11ErrorTrap();

    X11ErrorTrap(const X11ErrorTrap &) = delete;
    X11ErrorTrap &operator=(const X11ErrorTrap &) = delete;

    // Synchronizes with the X server (XSync), then returns true if an X error was recorded since
    // the trap was installed.
    bool sync_and_check();
    // Description of the first recorded error, filled in by sync_and_check(). Empty if none.
    const std::string &error() const { return m_error; }

private:
    // Display*, opaque here to keep the Xlib headers out. nullptr if the trap is not installed.
    void       *m_display{ nullptr };
    std::string m_error;
};

} // namespace GUI
} // namespace Slic3r

#endif // __WXGTK__

#endif // slic3r_GUI_X11ErrorTrap_hpp_
