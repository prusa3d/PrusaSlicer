#pragma once

#include <functional>
#include <vector>

#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Workbench.hpp"

namespace Slic3r::Biz::Scene {

enum class SelectionMode
{
    Instance = 0,
    Volume
};

enum class SelectionReferenceFrame {
    Bed,
    Instance,
    Volume
};

enum class SelectionState
{
    Empty,
    SingleVolume,
    WholeInstance,
    MultipleVolumes, // But not whole instance.
    MultipleInstances,
};

struct ObjectSelection
{
    using ElementRefs = std::vector<Domain::ElementRef>;

    SelectionMode mode{SelectionMode::Instance};
    ElementRefs elements;

    [[nodiscard]] SelectionState state() const;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool is_selected(const Domain::ElementRef& ref) const;

    bool remove(const Domain::ElementRef& ref);

    [[nodiscard]] bool only_single_object() const;

    [[nodiscard]] bool contains_wipe_tower() const;

    [[nodiscard]] bool is_valid() const;

    bool operator==(const ObjectSelection& selection) const = default;
};

enum class CameraActionOnBedSelection : uint8_t
{
    None,
    CenterOnBed
};

struct BedSelection
{
    std::function<void(const BedSelection&)> on_change{[](const BedSelection&) {}};

    Domain::BedRef last_selected_bed() const;

    bool is_selected(const Domain::BedRef bed_ref) const;

    bool empty() const;

    /** @brief Replace the selection with one. */
    bool select_one(const Domain::BedRef& bed_ref, CameraActionOnBedSelection camera_action = CameraActionOnBedSelection::None);

    /** @brief Add or remove from active selection. */
    bool toggle(const Domain::BedRef& bed_ref, CameraActionOnBedSelection camera_action = CameraActionOnBedSelection::None);

    bool remove(const Domain::BedRef& bed_ref);

    CameraActionOnBedSelection camera_action_on_selection() const { return m_camera_action_on_selection; }

    void set_state(
        Domain::BedRefs selected_beds,
        Domain::BedRef last_selected_bed,
        CameraActionOnBedSelection camera_action_on_selection
    );

    const Domain::BedRefs& selected_beds() const
    {
        return m_selected_beds;
    }

private:
    Domain::BedRefs m_selected_beds;
    Domain::BedRef m_last_selected_bed{Domain::INVALID_ID, Domain::INVALID_ID};
    CameraActionOnBedSelection m_camera_action_on_selection{ CameraActionOnBedSelection::None };
};

using BedInstanceRefWrap = std::reference_wrapper<const Domain::BedInstance>;
using BedInstances       = std::vector<BedInstanceRefWrap>;

} // namespace Slic3r::Biz::Scene
