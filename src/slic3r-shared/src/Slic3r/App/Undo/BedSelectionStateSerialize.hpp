#pragma once

#include "Slic3r/App/Undo/SerializedData.hpp"
#include "Slic3r/App/Undo/BedSelectionState.hpp"

namespace Slic3r::App::Undo {

SerializedData serialize_bed_selection_state(
    const BedSelectionState& value,
    const Domain::Project::ConfigContainerList& config_containers);

BedSelectionState load_serialized_bed_selection_state(
    const SerializedData& data,
    const Domain::Project::ConfigContainerList& config_containers);

} // namespace Slic3r::App::Undo
