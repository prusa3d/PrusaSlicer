#include "Slic3r/Domain/BedInstance.hpp"

namespace Slic3r::Domain {

BedInstance::BedInstance(const Bed& bed) : bed(bed) {}

std::unique_ptr<BedInstance> BedInstance::copy(
    const std::unordered_map<const Bed*, const Bed*>& bed_translation_table,
    const std::unordered_map<ModelInstance*, ModelInstance*>& model_instance_translation_table
) const
{
    std::unique_ptr<BedInstance> bed_instance =
        std::make_unique<BedInstance>(*bed_translation_table.at(&bed.get()));
    bed_instance->transformation = transformation;

    bed_instance->model_instances.reserve(model_instances.size());
    std::ranges::transform(
        model_instances,
        std::back_inserter(bed_instance->model_instances),
        [&](ModelInstance* model_instance)
        { return model_instance_translation_table.at(model_instance); }
    );

    bed_instance->colliding_instances.reserve(colliding_instances.size());
    std::ranges::transform(
        colliding_instances,
        std::inserter(bed_instance->colliding_instances, bed_instance->colliding_instances.end()),
        [&](ModelInstance* model_instance)
        { return model_instance_translation_table.at(model_instance); }
    );

    bed_instance->print_volume_enabled  = print_volume_enabled;
    bed_instance->wipe_tower            = wipe_tower;
    bed_instance->custom_gcode          = custom_gcode;
    bed_instance->extruder_candidates   = extruder_candidates;
    bed_instance->wipe_tower_is_outside = wipe_tower_is_outside;

    bed_instance->set_index(index());

    return bed_instance;
}

size_t BedInstance::index() const
{
    return m_index;
}

void BedInstance::set_index(size_t index)
{
    if (m_index != index) {
        m_index = index;
        m_label = std::to_string(index);
        m_name  = "Bed " + m_label;
    }
}

const std::string& BedInstance::label() const
{
    return m_label;
}

const std::string& BedInstance::name() const
{
    return m_name;
}

} // namespace Slic3r::Domain
