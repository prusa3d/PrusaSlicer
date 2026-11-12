#pragma once

#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {
class ISelectedBedInstancesChangedListener
{
public:
    virtual ~ISelectedBedInstancesChangedListener() = default;

    virtual void on_selected_bed_instances_changed(Domain::SelectionId project_id, const Scene::BedSelection& bed_selection) = 0;
};
}
