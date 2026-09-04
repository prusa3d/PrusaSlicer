#pragma once
#include <vector>

#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/Material.hpp"

namespace Slic3r::App::Render {

struct DrawCommand
{
    PrimitiveType primitive{PrimitiveType::Triangles};
    size_t offset{0};
    size_t count{0};
    Material material;
};

using DrawCommands = std::vector<DrawCommand>;

inline DrawCommands resolve_material(const DrawCommands& draw_commands, const Material& material_override)
{
    DrawCommands ret;
    ret.reserve(draw_commands.size());
    std::transform(
        draw_commands.begin(), draw_commands.end(), std::back_inserter(ret),
        [&](const auto& draw_command) {
            auto resolved = draw_command;
            resolved.material.update(material_override);
            return resolved;
        }
    );
    return ret;
}

} // namespace Slic3r::App::Render
