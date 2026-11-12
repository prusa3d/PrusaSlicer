#pragma once

#include "Slic3r/App/Undo/SerializedData.hpp"
#include "Slic3r/App/Undo/ToolState.hpp"

#include <vector>

namespace Slic3r::App::Undo {

SerializedData serialize_tools_state(const ToolsState& tools_state);

ToolsState load_serialized_tools_state(const SerializedData& data);

} // namespace Slic3r::App::Undo
