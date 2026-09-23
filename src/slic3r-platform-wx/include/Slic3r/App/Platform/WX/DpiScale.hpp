#pragma once

class wxWindow;

namespace Slic3r::App::Platform::WX {

/**
 * @brief Helper DpiScale class for handling DPI scale accross platforms
 */
struct DpiScale
{
    double buffer_scale{0}; //!< On Linux contains gtk_widget_get_scale_factor, always 1 outside GTK
    double dpi_scale{0}; ///< On Linux fractional remainder, full DPI scale factor outside GTK

    double total() const
    {
        return buffer_scale * dpi_scale;
    }
};

DpiScale get_dpi_scale(wxWindow* window);

} // namespace Slic3r::App::Platform::WX
