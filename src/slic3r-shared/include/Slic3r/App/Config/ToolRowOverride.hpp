#pragma once

#include <imgui.h>

#include <vector>
#include <string>

namespace Slic3r::Domain {
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::App {

struct ToolRowOverride
{
    const Domain::ConfigItem* override_item{nullptr};
    bool extruder_candidate{false};
    size_t tool_index{0};
    ImColor color{IM_COL32_WHITE};

    std::string dnd_key() const;
};

using ToolRowOverrides = std::vector<ToolRowOverride*>;

using ToolRowOverrideGroup = std::pair<ToolRowOverrides, size_t>;

} // namespace Slic3r::App
