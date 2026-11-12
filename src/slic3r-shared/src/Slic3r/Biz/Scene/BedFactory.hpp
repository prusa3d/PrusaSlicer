#pragma once

#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Types.hpp"
#include <string>
#include <functional>

namespace Slic3r::Domain {
class Bed;
class BedContainer;
class ConfigContainer;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Scene {

Domain::Bed&
get_or_create_bed(Domain::BedContainer& bed_container, const Domain::ConfigContainer& config_container, const std::string& assets_path,
    Domain::SelectionId project_id = Domain::INVALID_ID, Domain::SelectionId config_container_id = Domain::INVALID_ID,
    std::function<Domain::Vec2ds(Domain::SelectionId, Domain::SelectionId)> system_preset_bed_shape_getter = nullptr);

} // namespace Slic3r::Biz::Scene
