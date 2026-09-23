#pragma once

#include "Slic3r/Domain/Color.hpp"

#include <optional>

#include "Slic3r/Domain/MatDb/Types.hpp"

namespace Slic3r::Domain::MatDb {

struct Brand
{
    std::string uuid;
    std::string slug;
    std::string name;
};

struct Color
{
    std::string color_rgba;
    ColorRGBA native;
};

struct Material
{
    std::string uuid;
    std::string slug;
    std::string name;

    Brand brand;

    std::string klass;
    std::optional<std::string> type;
    std::string abbreviation;
    std::optional<Color> primary_color;
    std::vector<Color> secondary_colors;
    StringList tags;
};


}
