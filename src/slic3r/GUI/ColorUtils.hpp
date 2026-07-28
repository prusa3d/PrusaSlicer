#pragma once

#include <string>

#include <wx/colour.h>

namespace Slic3r::GUI {

double wcag_luminance(const wxColour& color);
wxColour wcag_contrast_text(const wxColour& background);
wxColour parse_hex_color(const std::string& hex);
std::string wxcolour_to_hex(const wxColour& color);

} // namespace Slic3r::GUI
