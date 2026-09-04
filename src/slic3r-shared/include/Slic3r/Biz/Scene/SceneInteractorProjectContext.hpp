#pragma once

#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Biz/Scene/SelectionExtents.hpp"
#include "libslic3r/WipeTowerGeometry.hpp"

namespace Slic3r::Domain { class Project; }


namespace Slic3r::Biz::Scene {

/**
 * @brief Transient preview of a bed that *would* be added if a drag drops on it.
 *        Not part of domain state: never serialized, never in undo stack.
 */
struct VirtualBedPreview
{
    Domain::SelectionId config_container_id;
    Domain::Transform3d transform;
};

struct SceneInteractorProjectContext
{
    Domain::Project& project;
    BedSelection bed_selection;
    ObjectSelection object_selection;
    std::optional<SelectionExtents> object_selection_bounding_box;
    SelectionReferenceFrame object_selection_reference_frame{SelectionReferenceFrame::Bed};

    // key is bed_instance_id
    std::map<std::size_t, Biz::Slicing::WipeTowerGeometry> wipe_tower_geometries;

    std::optional<VirtualBedPreview> virtual_bed_preview;
};

}
