#pragma once

#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/App/Undo/SerializedData.hpp"

namespace Slic3r::Biz::Preset {
    class PresetInteractor;
}

namespace Slic3r::App::Undo {
SerializedData serialize_config_container_list(const Domain::Project::ConfigContainerList& value);

Domain::Project::ConfigContainerList load_serialized_config_container_list(
    Domain::SelectionId project_id,
    const SerializedData& data,
    Domain::BedContainer& bed_container,
    Biz::Preset::PresetInteractor& preset_interactor
);

} // namespace Slic3r::App::Undo
