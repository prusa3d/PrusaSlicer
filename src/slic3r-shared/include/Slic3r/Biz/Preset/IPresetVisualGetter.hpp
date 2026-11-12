#pragma once

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <cstddef>

namespace Slic3r::Biz::Preset {

class IPresetVisualGetter
{
public:
    virtual ~IPresetVisualGetter() = default;
    virtual Domain::Vec2ds system_preset_bed_shape(Domain::SelectionId project_id, Domain::SelectionId config_container_id) const = 0;
};

} // namespace Slic3r::Biz::Preset
