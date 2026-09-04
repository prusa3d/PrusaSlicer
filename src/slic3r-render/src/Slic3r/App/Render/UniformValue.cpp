#include "Slic3r/App/Render/UniformValue.hpp"
#include "Slic3r/App/Render/Shader.hpp"

#include <type_traits>

namespace Slic3r::App::Render {

void set_uniform(const Shader& shader, const char* param_name, const UniformValue& value)
{
    std::visit([param_name, &shader](auto&& arg) {
        shader.set_uniform(param_name, arg);
    }, value);
}

}
