#include "ColorUtils.hpp"

#include <wx/string.h>

namespace Slic3r::GUI {

double wcag_luminance(const wxColour& color)
{
    return 0.2126 * (color.Red() / 255.0)
        + 0.7152 * (color.Green() / 255.0)
        + 0.0722 * (color.Blue() / 255.0);
}

wxColour wcag_contrast_text(const wxColour& background)
{
    return wcag_luminance(background) > 0.55 ? wxColour(0, 0, 0) : wxColour(255, 255, 255);
}

wxColour parse_hex_color(const std::string& hex)
{
    if (hex.size() == 7 && hex[0] == '#') {
        unsigned long rgb = 0;
        try {
            rgb = std::stoul(hex.substr(1), nullptr, 16);
        } catch (...) {
            return wxColour(0x80, 0x80, 0x80);
        }
        return wxColour(
            static_cast<unsigned char>((rgb >> 16) & 0xFF),
            static_cast<unsigned char>((rgb >> 8) & 0xFF),
            static_cast<unsigned char>(rgb & 0xFF)
        );
    }
    return wxColour(0x80, 0x80, 0x80);
}

std::string wxcolour_to_hex(const wxColour& color)
{
    return wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue())
        .ToStdString();
}

} // namespace Slic3r::GUI
