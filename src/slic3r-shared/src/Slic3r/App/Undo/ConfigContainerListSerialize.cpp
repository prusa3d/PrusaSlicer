#include "Slic3r/App/Undo/ConfigContainerListSerialize.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/App/Undo/SerializationUtils.hpp"
#include "Slic3r/Biz/Scene/BedTracking.hpp"
#include "Slic3r/Biz/Scene/BedFactory.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#define CEREAL_FUTURE_EXPERIMENTAL
#include <cereal/archives/adapters.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>

using Slic3r::App::Undo::Chunk;
using Slic3r::App::Undo::SerializedData;
using Slic3r::App::Undo::VirtualExtrudersId;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderGradient;
using Slic3r::Domain::VirtualExtruderGradientStop;

struct DeserializationContext
{
    Slic3r::App::Undo::SerializedData snapshot;
    Slic3r::Domain::BedContainer& bed_container;
    Slic3r::Domain::SelectionId project_id;
    Slic3r::Biz::Preset::PresetInteractor& preset_interactor;
};

namespace cereal {

template <class Archive>
void save(Archive& archive, const Slic3r::Domain::BedInstance& bed_instance)
{
    archive(cereal::base_class<Slic3r::Domain::ObjectBase>(&bed_instance));
    archive(
        bed_instance.transformation,
        bed_instance.print_volume_enabled,
        bed_instance.wipe_tower,
        // bed_instance.custom_gcode TODO
        bed_instance.index()
    );
}

template <class Archive>
void load(Archive& archive, Slic3r::Domain::BedInstance& bed_instance)
{
    archive(cereal::base_class<Slic3r::Domain::ObjectBase>(&bed_instance));
    archive(
        bed_instance.transformation,
        bed_instance.print_volume_enabled,
        bed_instance.wipe_tower
        // bed_instance.custom_gcode TODO
    );

    std::size_t index{};
    archive(index);

    bed_instance.set_index(index);
}

template <class Archive>
struct specialize<
    Archive,
    Slic3r::Domain::BedInstance,
    cereal::specialization::non_member_load_save>
{};

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::SelectedPreset& preset)
{
    archive(preset.hw_config, preset.printer, preset.print, preset.tools, preset.materials);
}

template <class Archive>
void serialize(Archive& ar, VirtualExtruderComponent& component)
{
    ar(component.extruder_id, component.ratio);
}

template <class Archive>
void serialize(Archive& ar, VirtualExtruderGradientStop& stop)
{
    ar(stop.extruder_id, stop.position);
}

template <class Archive>
void serialize(Archive& ar, VirtualExtruderGradient& gradient)
{
    ar(gradient.z_min, gradient.z_max, gradient.stops);
}

template <class Archive>
void serialize(Archive& ar, VirtualExtruder& virtual_extruder)
{
    ar(virtual_extruder.id,
       virtual_extruder.color,
       virtual_extruder.components,
       virtual_extruder.gradient);
}

} // namespace cereal

static void save_bed_instances(
    cereal::BinaryOutputArchive& archive,
    const Slic3r::Domain::ConfigContainer::BedInstanceList& bed_instances,
    std::size_t id
)
{
    using Slic3r::App::Undo::SerializedData;
    using OutputArchive = cereal::UserDataAdapter<SerializedData, cereal::BinaryOutputArchive>;
    using Slic3r::App::Undo::ConfigContainerChunk;
    using Slic3r::Domain::BedInstance;

    SerializedData& snapshot{get_user_data<SerializedData>(archive)};

    const Slic3r::App::Undo::BedInstancesId channel_id{id};

    std::ostringstream oss;
    OutputArchive sub_archive{snapshot, oss};

    sub_archive(bed_instances.size());
    for (const std::unique_ptr<BedInstance>& bed_instance : bed_instances) {
        sub_archive(*bed_instance);
    }

    snapshot.separate_chunks[channel_id] = oss.str();
    archive(channel_id);
}

static void load_bed_instances(
    cereal::BinaryInputArchive& archive,
    Slic3r::Domain::ConfigContainer& config_container
)
{
    using Slic3r::App::Undo::Chunk;
    using Slic3r::App::Undo::ConfigContainerChunk;
    using Slic3r::App::Undo::SerializedData;
    using InputArchive =
        cereal::UserDataAdapter<DeserializationContext, cereal::BinaryInputArchive>;

    DeserializationContext& context{get_user_data<DeserializationContext>(archive)};
    SerializedData& snapshot{context.snapshot};

    Slic3r::App::Undo::BedInstancesId id{};
    archive(id);

    const Chunk& chunk{snapshot.separate_chunks.at(id)};

    const std::string& serialized_data{std::get<std::string>(chunk)};
    std::istringstream iss(serialized_data);
    InputArchive sub_archive{context, iss};
    std::size_t bed_instances_size{};
    sub_archive(bed_instances_size);

    for (std::size_t i{}; i < bed_instances_size; ++i) {
        Slic3r::Domain::BedInstance& bed_instance{config_container.add_bed_instance()};
        sub_archive(bed_instance);
    }
}

static void save_virtual_extruders(
    cereal::BinaryOutputArchive& archive,
    const ConfigContainer& config_container,
    std::size_t id
)
{
    SerializedData& snapshot{get_user_data<SerializedData>(archive)};

    const VirtualExtrudersId channel_id{id};

    std::ostringstream oss;
    cereal::BinaryOutputArchive sub_archive{oss};
    sub_archive(config_container.virtual_extruders());

    snapshot.separate_chunks[channel_id] = oss.str();
    archive(channel_id);
}

static void
load_virtual_extruders(cereal::BinaryInputArchive& archive, ConfigContainer& config_container)
{
    DeserializationContext& context{get_user_data<DeserializationContext>(archive)};
    SerializedData& snapshot{context.snapshot};

    VirtualExtrudersId id{};
    archive(id);

    const Chunk& chunk{snapshot.separate_chunks.at(id)};

    const std::string& serialized_data{std::get<std::string>(chunk)};
    std::istringstream iss(serialized_data);
    cereal::BinaryInputArchive sub_archive{iss};
    sub_archive(config_container.virtual_extruders());
}

static void save_config_container(
    cereal::BinaryOutputArchive& archive,
    const Slic3r::Domain::ConfigContainer& config_container,
    std::size_t id
)
{
    using Slic3r::App::Undo::SerializedData;
    using OutputArchive = cereal::UserDataAdapter<SerializedData, cereal::BinaryOutputArchive>;
    using Slic3r::App::Undo::ConfigContainerChunk;

    SerializedData& snapshot{get_user_data<SerializedData>(archive)};

    const Slic3r::App::Undo::ConfigContainerId channel_id{id};

    std::ostringstream oss;
    OutputArchive sub_archive{snapshot, oss};
    sub_archive(
        cereal::base_class<Slic3r::Domain::ObjectBase>(&config_container),
        config_container.selected_preset(),
        config_container.bed().id().id
    );
    save_bed_instances(sub_archive, config_container.bed_instances(), id);
    save_virtual_extruders(sub_archive, config_container, id);

    snapshot.separate_chunks[channel_id] = ConfigContainerChunk{
        oss.str(),
        {config_container.selected_preset().hw_config.id, config_container.id().id}};
    archive(channel_id);
}

static void load_config_container(
    cereal::BinaryInputArchive& archive,
    Slic3r::Domain::ConfigContainer& config_container
)
{
    using Slic3r::App::Undo::Chunk;
    using Slic3r::App::Undo::ConfigContainerChunk;
    using Slic3r::App::Undo::SerializedData;
    using InputArchive =
        cereal::UserDataAdapter<DeserializationContext, cereal::BinaryInputArchive>;
    using Slic3r::Domain::Bed;

    DeserializationContext& context{get_user_data<DeserializationContext>(archive)};
    SerializedData& snapshot{context.snapshot};

    Slic3r::App::Undo::ConfigContainerId id{};
    archive(id);

    const Chunk& chunk{snapshot.separate_chunks.at(id)};

    const std::string serialized_data{std::get<ConfigContainerChunk>(chunk).serialized_data};
    std::istringstream iss(serialized_data);
    InputArchive sub_archive{context, iss};

    sub_archive(cereal::base_class<Slic3r::Domain::ObjectBase>(&config_container));
    sub_archive(config_container.mutable_selected_preset());

    std::size_t bed_id{};
    sub_archive(bed_id);
    const Bed* bed{context.bed_container.bed(bed_id)};
    if (!bed) {
        bed = &Slic3r::Biz::Scene::get_or_create_bed(
            context.bed_container,
            config_container,
            Slic3r::resources_dir(),
            context.project_id,
            config_container.id().id,
            [&](Slic3r::Domain::SelectionId project_id,
                Slic3r::Domain::SelectionId config_container_id)
            {
                return config_container.selected_preset()
                    .printer.config_box()
                    .items.opt("bed_shape")
                    .get<Slic3r::Domain::Vec2ds>();
            }
        );
    }

    config_container.set_bed(*bed);

    load_bed_instances(sub_archive, config_container);
    load_virtual_extruders(sub_archive, config_container);
}

namespace Slic3r::App::Undo {

SerializedData serialize_config_container_list(const Domain::Project::ConfigContainerList& list)
{
    SerializedData snapshot;
    using OutputArchive = cereal::UserDataAdapter<SerializedData, cereal::BinaryOutputArchive>;

    std::ostringstream oss;
    OutputArchive archive{snapshot, oss};

    archive(list.size());
    for (std::size_t index{}; index < list.size(); ++index) {
        const Domain::ConfigContainer& config_container{*list[index]};
        save_config_container(archive, config_container, index + 1);
    }

    snapshot.serialized_data = oss.str();

    return snapshot;
}

Domain::Project::ConfigContainerList load_serialized_config_container_list(
    Domain::SelectionId project_id,
    const SerializedData& data,
    Domain::BedContainer& bed_container,
    Biz::Preset::PresetInteractor& preset_interactor

)
{
    using InputArchive =
        cereal::UserDataAdapter<DeserializationContext, cereal::BinaryInputArchive>;

    DeserializationContext context{data, bed_container, project_id, preset_interactor};

    std::istringstream iss(data.serialized_data);
    InputArchive archive(context, iss);

    Domain::Project::ConfigContainerList result;

    std::size_t list_size{};
    archive(list_size);
    for (std::size_t index{}; index < list_size; ++index) {
        Domain::ConfigContainer config_container;
        load_config_container(archive, config_container);
        result.push_back(std::make_unique<Domain::ConfigContainer>(std::move(config_container)));
    }

    return result;
}

} // namespace Slic3r::App::Undo
