#include "Slic3r/App/Undo/BedSelectionStateSerialize.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

namespace {
struct BedIndex
{
    std::size_t container_index{};
    std::size_t instance_index{};
};
} // namespace

namespace cereal {

template <class Archive>
void serialize(Archive& ar, BedIndex& bed_index)
{
    ar(bed_index.container_index, bed_index.instance_index);
}

} // namespace cereal

namespace Slic3r::App::Undo {

static std::size_t container_id_to_index(std::size_t id,
                                  const Domain::Project::ConfigContainerList& config_containers)
{
    if (id == Domain::INVALID_ID) {
        return Domain::INVALID_ID;
    }

    for (std::size_t index{}; index < config_containers.size(); ++index) {
        const auto& container = config_containers[index];
        if (container->id().id == id) {
            return index;
        }
    }
    PANIC("Container not found");
    return {};
}

static BedIndex bed_ref_to_bed_index(const Domain::BedRef& bed_ref,
                              const Domain::Project::ConfigContainerList& config_containers)
{
    if (bed_ref.config_container_id == Domain::INVALID_ID
        || bed_ref.instance_id == Domain::INVALID_ID)
    {
        return {Domain::INVALID_ID, Domain::INVALID_ID};
    }

    std::size_t container_index{
        container_id_to_index(bed_ref.config_container_id, config_containers)};
    const auto& container{config_containers.at(container_index)};

    const auto& bed_instances{container->bed_instances()};
    for (std::size_t bed_index{}; bed_index < bed_instances.size(); ++bed_index) {
        if (bed_instances[bed_index]->id().id == bed_ref.instance_id) {
            return {container_index, bed_index};
        }
    }

    PANIC("Bed not found");
    return {};
}

static Domain::BedRef bed_index_to_bed_ref(const BedIndex& bed_index,
                                    const Domain::Project::ConfigContainerList& config_containers)
{
    if (bed_index.container_index == Domain::INVALID_ID
        || bed_index.instance_index == Domain::INVALID_ID)
        return {Domain::INVALID_ID, Domain::INVALID_ID};

    ASSERT(bed_index.container_index < config_containers.size());

    const auto& container{config_containers[bed_index.container_index]};
    const auto& bed_instances{container->bed_instances()};
    ASSERT(bed_index.instance_index < bed_instances.size());

    return {container->id().id, bed_instances[bed_index.instance_index]->id().id};
}

SerializedData serialize_bed_selection_state(
    const BedSelectionState& value,
    const Domain::Project::ConfigContainerList& config_containers)
{
    std::ostringstream oss;
    cereal::BinaryOutputArchive archive{oss};

    archive(value.selected_beds.size());
    for (const Domain::BedRef& bed_ref : value.selected_beds) {
        const BedIndex index{bed_ref_to_bed_index(bed_ref, config_containers)};
        archive(index);
    }
    archive(bed_ref_to_bed_index(value.last_selected_bed, config_containers));
    archive(value.camera_action_on_selection);

    SerializedData snapshot;
    snapshot.serialized_data = oss.str();

    return snapshot;
}

BedSelectionState load_serialized_bed_selection_state(
    const SerializedData& data,
    const Domain::Project::ConfigContainerList& config_containers)
{
    std::istringstream iss(data.serialized_data);
    cereal::BinaryInputArchive archive(iss);

    BedSelectionState result;

    std::size_t selected_beds_size{};
    archive(selected_beds_size);
    result.selected_beds.resize(selected_beds_size);
    for (Domain::BedRef& bed_ref : result.selected_beds) {
        BedIndex index;
        archive(index);
        bed_ref = bed_index_to_bed_ref(index, config_containers);
    }
    BedIndex bed_index{};
    archive(bed_index);
    result.last_selected_bed = bed_index_to_bed_ref(bed_index, config_containers);
    archive(result.camera_action_on_selection);

    return result;
}

} // namespace Slic3r::App::Undo
