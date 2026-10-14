///|/ Copyright (c) Prusa Research 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

class wxMenu;

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
/// Shows the images of the menu's items, which AppKit hides by default in apps built with the
/// macOS 27 SDK or later.
extern void mac_show_menu_item_images(wxMenu* menu);
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
