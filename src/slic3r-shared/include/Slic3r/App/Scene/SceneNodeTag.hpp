#pragma once

#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::App::Scene {

struct SceneNodeTag
{
    const Domain::SelectionId object_id{0};
    const Domain::SelectionId volume_id{0};
    const Domain::SelectionId instance_id{0};
    const Domain::ModelVolumeType volume_type{Domain::ModelVolumeType::INVALID};
    const Domain::SlicingId wipe_tower_id{};

    bool matches_element(const Domain::ElementRef& e) const
    {
        return e.object_id == object_id && e.instance_id == instance_id && e.volume_id == volume_id;
    }

    bool is_wipe_tower() const
    {
        return wipe_tower_id != Domain::SlicingId{};
    }
};

} // namespace Slic3r::App::Scene
