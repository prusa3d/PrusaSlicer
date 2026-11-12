#include "Slic3r/App/Platform/WX/DpiScale.hpp"

#include <wx/window.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace Slic3r::App::Platform::WX {

DpiScale get_dpi_scale(wxWindow* window)
{
#ifdef __WXGTK__
    GtkWidget* widget = window->GetHandle();
    return {
        static_cast<double>(gtk_widget_get_scale_factor(widget)),
        gdk_screen_get_resolution(gtk_widget_get_screen(widget)) / 96.0
    };
#else
    return {1.0, window->GetDPIScaleFactor()};
#endif
}

}
