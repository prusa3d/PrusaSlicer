#pragma once

#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/CustomGCode.hpp"
#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Transformation.hpp"

#include <unordered_set>

namespace Slic3r::Domain {

// TODO: move this to better place
using ModelInstanceList      = std::vector<ModelInstance*>;
using ModelInstanceSet       = std::unordered_set<ModelInstance*>;
using ConstModelInstanceList = std::vector<const ModelInstance*>;

class Bed;

struct ExtruderCandidate
{
    uint8_t tool_index;
    uint8_t slot_index;
};

using ExtruderCandidates = std::vector<ExtruderCandidate>;

struct BedInstance : public ObjectBase
{
    explicit BedInstance(const Bed& bed);

    std::unique_ptr<BedInstance> copy(
        const std::unordered_map<const Bed*, const Bed*>& bed_translation_table,
        const std::unordered_map<ModelInstance*, ModelInstance*>& model_instance_translation_table
    ) const;

    const Transform3d& matrix() const
    {
        return transformation.get_matrix();
    }

    size_t index() const;
    void set_index(size_t index);

    const std::string& label() const;
    const std::string& name() const;

    std::reference_wrapper<const Bed> bed;
    Transformation transformation;
    ModelInstanceList model_instances;
    ModelInstanceSet colliding_instances;
    bool print_volume_enabled{false};
    ModelWipeTower wipe_tower{};
    std::optional<CustomGCode::Info> custom_gcode;
    std::vector<unsigned> extruder_candidates;

    /** Indicates whether the wipe tower is partially or fully outside the bed. */
    bool wipe_tower_is_outside{false};

private:
    size_t m_index{0};
    std::string m_label;
    std::string m_name;
};

} // namespace Slic3r::Domain
