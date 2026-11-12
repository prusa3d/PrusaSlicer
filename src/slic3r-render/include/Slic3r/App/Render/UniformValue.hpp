#pragma once

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Render/Types.hpp"

#include <array>
#include <string_view>
#include <variant>

namespace Slic3r::App::Render {

class Shader;

using UniformValue = std::variant<
    float
    , int
    , bool
    , Domain::Vec2f
    , Domain::Vec3f
    , Domain::Vec4f
    , Domain::SquareMatrix3f
    , Domain::SquareMatrix4f
    , Domain::ColorRGB
    , Domain::ColorRGBA

>;

void set_uniform(const Shader& shader, const char* param_name, const UniformValue& value);

}