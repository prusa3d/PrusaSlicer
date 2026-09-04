#pragma once

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Test {

struct Extrusion {
    Domain::Vec4d start;
    Domain::Vec4d end;
};

std::optional<std::string> are_statistics_sane(const std::string& gcode);
std::optional<std::string> is_gcode_sane(const std::string& gcode, const Domain::Model &model);

}
