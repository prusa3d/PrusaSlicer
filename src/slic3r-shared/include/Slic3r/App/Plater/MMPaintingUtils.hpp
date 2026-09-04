#pragma once

#include "Slic3r/Domain/Color.hpp"

#include <vector>

namespace Slic3r::App::Plater {

enum class SelectedColor
{
    None,
    Primary,
    Secondary,
    Both
};

struct PaletteEntry
{
    Domain::ColorRGBA color;
    unsigned int state = 0;
};

using PaintingPalette = std::vector<PaletteEntry>;

} // namespace Slic3r::App::Plater
