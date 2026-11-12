#pragma once

#include "Widgets/BitmapComboBox.hpp"
#include <wx/bmpbndl.h>

namespace Slic3r::App::WX {

wxBitmapBundle* get_bmp_bundle(const std::string& bmp_name, int width = 16, int height = -1, const std::string& new_color_rgb = std::string());
wxBitmapBundle* get_bmp_bundle_for_mac_menu(const std::string& bmp_name);
wxBitmapBundle* get_empty_bmp_bundle(int width, int height);
wxBitmapBundle* get_solid_bmp_bundle(int width, int height, const std::string& color);

std::vector<wxBitmapBundle*> get_extruder_color_icons(bool thin_icon = false);

inline wxSize get_preferred_size(const wxBitmapBundle& bmp, wxWindow* parent)
{
    if (!bmp.IsOk())
        return wxSize(0,0);
#ifdef __WIN32__
    return bmp.GetPreferredBitmapSizeFor(parent);
#else
    return bmp.GetDefaultSize();
#endif

void apply_extruder_selector(Widgets::BitmapComboBox**   ctrl,
                             wxWindow*          parent,
                             bool               use_full_item_name = false,
                             const std::string& first_item = "",
                             wxPoint            pos = wxDefaultPosition,
                             wxSize             size = wxDefaultSize,
                             bool               use_thin_icon = false);

}

}
