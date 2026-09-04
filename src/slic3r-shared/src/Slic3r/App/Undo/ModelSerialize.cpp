#include "Slic3r/Biz/CerealUtils.hpp"

#define CEREAL_FUTURE_EXPERIMENTAL
#include <cereal/archives/adapters.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "Slic3r/App/Undo/ModelSerialize.hpp"
#include "Slic3r/App/Undo/SerializationUtils.hpp"

struct SerializationContext
{
    Slic3r::App::Undo::SerializedData& current_snapshot;
    const Slic3r::App::Undo::SerializedData& previous_snapshot;
};

template <class T>
static void
save_separate_chunk(cereal::BinaryOutputArchive& archive, const T& value, std::size_t id)
{
    using OutputArchive =
        cereal::UserDataAdapter<SerializationContext, cereal::BinaryOutputArchive>;
    SerializationContext& context{get_user_data<SerializationContext>(archive)};
    Slic3r::App::Undo::SerializedData& snapshot{context.current_snapshot};

    const Slic3r::App::Undo::ObjectId channel_id{id};

    std::ostringstream oss;
    OutputArchive sub_archive{context, oss};
    sub_archive(value);

    snapshot.separate_chunks[channel_id] = oss.str();
    archive(channel_id);
}

template <class T>
static void load_separate_chunk(cereal::BinaryInputArchive& archive, T& value)
{
    using Slic3r::App::Undo::Chunk;
    using Slic3r::App::Undo::SerializedData;
    using Slic3r::App::Undo::VersionedChunk;
    using InputArchive = cereal::UserDataAdapter<SerializedData, cereal::BinaryInputArchive>;
    SerializedData& snapshot{get_user_data<SerializedData>(archive)};

    Slic3r::App::Undo::ObjectId id{};
    archive(id);

    const Chunk& chunk{snapshot.separate_chunks.at(id)};

    const std::string* serialized_data{};
    if (std::holds_alternative<std::string>(chunk)) {
        serialized_data = &std::get<std::string>(chunk);
    } else if (std::holds_alternative<VersionedChunk>(chunk)) {
        serialized_data = &std::get<VersionedChunk>(chunk).serialized_data;
    } else {
        PANIC("Invalid serialized chunk!");
    }
    std::istringstream iss(*serialized_data);
    InputArchive sub_archive{snapshot, iss};
    sub_archive(value);
}

template <class T>
static void save_separate_chunk_with_version(
    cereal::BinaryOutputArchive& archive,
    const T& value,
    std::size_t id,
    std::size_t version
)
{
    using OutputArchive =
        cereal::UserDataAdapter<SerializationContext, cereal::BinaryOutputArchive>;
    SerializationContext& context{get_user_data<SerializationContext>(archive)};
    Slic3r::App::Undo::SerializedData& snapshot{context.current_snapshot};

    const Slic3r::App::Undo::ObjectId channel_id{id};
    bool needs_serialization{true};
    auto it{context.previous_snapshot.separate_chunks.find(channel_id)};
    if (it != context.previous_snapshot.separate_chunks.end()) {
        using Slic3r::App::Undo::VersionedChunk;
        const auto& previous_chunk{std::get<VersionedChunk>(it->second)};
        if (previous_chunk.version == version) {
            needs_serialization = false;
        }
    }

    std::string serialized_data;
    if (needs_serialization) {
        std::ostringstream oss;
        OutputArchive sub_archive{context, oss};
        sub_archive(value);
        serialized_data = oss.str();
    }

    snapshot.separate_chunks[channel_id] = Slic3r::App::Undo::VersionedChunk{serialized_data, version};
    archive(channel_id);
}

static void save_triangle_mesh(
    cereal::BinaryOutputArchive& archive,
    const std::shared_ptr<const Slic3r::Domain::TriangleMesh>& pointer
)
{
    SerializationContext& context{get_user_data<SerializationContext>(archive)};
    Slic3r::App::Undo::SerializedData& snapshot{context.current_snapshot};

    ASSERT(pointer);
    const std::size_t raw_ptr_value{
        static_cast<std::size_t>(reinterpret_cast<std::ptrdiff_t>(pointer.get()))
    };
    const Slic3r::App::Undo::TriangleMeshId channel_id{raw_ptr_value};
    snapshot.separate_chunks[channel_id] = Slic3r::App::Undo::TriangleMeshChunk{pointer};
    archive(channel_id);
}

static void load_triangle_mesh(
    cereal::BinaryInputArchive& archive,
    std::shared_ptr<const Slic3r::Domain::TriangleMesh>& pointer
)
{
    using Slic3r::App::Undo::Chunk;
    using Slic3r::App::Undo::SerializedData;
    SerializedData& snapshot{get_user_data<SerializedData>(archive)};

    Slic3r::App::Undo::TriangleMeshId id{};
    archive(id);

    const auto& chunk{
        std::get<Slic3r::App::Undo::TriangleMeshChunk>(snapshot.separate_chunks.at(id))
    };
    pointer = chunk.mesh;
}

namespace cereal {

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::CutId& cut_id)
{
    ar(cut_id.m_unique_id, cut_id.m_check_sum, cut_id.m_connectors_cnt);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::CutConnectorAttributes& attributes)
{
    ar(attributes.type, attributes.style, attributes.shape);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::CutConnector& connector)
{
    ar(connector.pos,
       connector.rotation_m,
       connector.radius,
       connector.height,
       connector.radius_tolerance,
       connector.height_tolerance,
       connector.z_angle,
       connector.attribs);
}

template <class Archive, Slic3r::Domain::BoundingBoxConcept BoxType>
void serialize(Archive& archive, BoxType& box)
{
    archive(box.min, box.max, box.defined);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::SLA::SupportPoint& point)
{
    ar(point.pos, point.head_front_radius, point.type);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::SLA::DrainHole& drain_hole)
{
    ar(drain_hole.pos, drain_hole.normal, drain_hole.radius, drain_hole.height, drain_hole.failed);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ModelVolume::Source& source)
{
    ar(source.input_file,
       source.object_idx,
       source.volume_idx,
       source.mesh_offset,
       source.transform,
       source.is_converted_from_inches,
       source.is_converted_from_meters,
       source.is_from_builtin_objects);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ModelVolume::CutInfo& cut_info)
{
    ar(cut_info.is_connector,
       cut_info.is_processed,
       cut_info.connector_type,
       cut_info.radius_tolerance,
       cut_info.height_tolerance);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::TextConfiguration& text_configuration)
{
    ar(text_configuration.style, text_configuration.text);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Polygon& polygon)
{
    archive(polygon.points);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::ExPolygon& expoly)
{
    archive(expoly.contour, expoly.holes);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ExPolygonsWithId& o)
{
    ar(o.id, o.expoly, o.is_healed);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::HealedExPolygons& o)
{
    ar(o.expolygons, o.is_healed);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::EmbossShape& shape)
{
    ar(shape.shapes_with_ids, shape.final_shape, shape.scale, shape.projection, shape.svg_file);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ModelInstance& model_instance)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model_instance),
       model_instance.m_transformation,
       model_instance.print_volume_state,
       model_instance.printable);
}

template <class Archive>
void load(Archive& ar, Slic3r::Domain::ModelVolume& model_volume)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model_volume),
       model_volume.name,
       model_volume.source);

    load_triangle_mesh(ar, model_volume.m_mesh);
    load_triangle_mesh(ar, model_volume.m_convex_hull);

    ar(model_volume.m_type,
       model_volume.m_transformation,
       model_volume.m_is_splittable,
       model_volume.cut_info,
       model_volume.volume_settings,
       model_volume.text_configuration,
       model_volume.emboss_shape);

    for (Slic3r::Domain::FacetsAnnotation* facet_annotation :
         {&model_volume.supported_facets,
          &model_volume.seam_facets,
          &model_volume.mm_segmentation_facets,
          &model_volume.fuzzy_skin_facets})
    {
        load_separate_chunk(ar, *facet_annotation);
    }
}

template <class Archive>
void save(Archive& ar, const Slic3r::Domain::ModelVolume& model_volume)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model_volume),
       model_volume.name,
       model_volume.source);

    save_triangle_mesh(ar, model_volume.m_mesh);
    save_triangle_mesh(ar, model_volume.m_convex_hull);

    ar(model_volume.m_type,
       model_volume.m_transformation,
       model_volume.m_is_splittable,
       model_volume.cut_info,
       model_volume.volume_settings,
       model_volume.text_configuration,
       model_volume.emboss_shape);

    for (const Slic3r::Domain::FacetsAnnotation* facet_annotation :
         {&model_volume.supported_facets,
          &model_volume.seam_facets,
          &model_volume.mm_segmentation_facets,
          &model_volume.fuzzy_skin_facets})
    {
        save_separate_chunk_with_version(
            ar,
            *facet_annotation,
            facet_annotation->id().id,
            facet_annotation->timestamp()
        );
    }
}

template <class Archive>
struct specialize<
    Archive,
    Slic3r::Domain::ModelVolume,
    cereal::specialization::non_member_load_save>
{};

template <class Archive>
void load(Archive& ar, Slic3r::Domain::ModelObject& model_object)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model_object),
       model_object.name,
       model_object.input_file);

    std::size_t instances_size{};
    ar(instances_size);
    model_object.clear_instances();
    for (std::size_t _{}; _ < instances_size; ++_) {
        Slic3r::Domain::ModelInstance* instance{new Slic3r::Domain::ModelInstance()};
        model_object.instances.push_back(instance);
        load_separate_chunk(ar, *instance);
    }

    std::size_t volumes_size{};
    ar(volumes_size);
    model_object.clear_volumes();
    for (std::size_t _{}; _ < volumes_size; ++_) {
        Slic3r::Domain::ModelVolume* volume{new Slic3r::Domain::ModelVolume()};
        load_separate_chunk(ar, *volume);
        model_object.volumes.push_back(volume);
    }

    ar(model_object.object_settings,
       model_object.object_settings_sla,
       model_object.sla_support_points,
       model_object.sla_points_status,
       model_object.sla_drain_holes,
       model_object.origin_translation,
       model_object.m_bounding_box_approx,
       model_object.m_bounding_box_approx_valid,
       model_object.m_bounding_box_exact,
       model_object.m_bounding_box_exact_valid,
       model_object.m_min_max_z_valid,
       model_object.m_raw_bounding_box,
       model_object.m_raw_bounding_box_valid,
       model_object.m_raw_mesh_bounding_box,
       model_object.m_raw_mesh_bounding_box_valid,
       model_object.cut_connectors,
       model_object.cut_id,
       model_object.layer_config_ranges);

    load_separate_chunk(ar, model_object.layer_height_profile);
}

template <class Archive>
void save(Archive& ar, const Slic3r::Domain::ModelObject& model_object)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model_object),
       model_object.name,
       model_object.input_file);

    ar(model_object.instances.size());
    for (Slic3r::Domain::ModelInstance* model_instance : model_object.instances) {
        save_separate_chunk(ar, *model_instance, model_instance->id().id);
    }

    ar(model_object.volumes.size());
    for (Slic3r::Domain::ModelVolume* model_volume : model_object.volumes) {
        save_separate_chunk(ar, *model_volume, model_volume->id().id);
    }

    ar(model_object.object_settings,
       model_object.object_settings_sla,
       model_object.sla_support_points,
       model_object.sla_points_status,
       model_object.sla_drain_holes,
       model_object.origin_translation,
       model_object.m_bounding_box_approx,
       model_object.m_bounding_box_approx_valid,
       model_object.m_bounding_box_exact,
       model_object.m_bounding_box_exact_valid,
       model_object.m_min_max_z_valid,
       model_object.m_raw_bounding_box,
       model_object.m_raw_bounding_box_valid,
       model_object.m_raw_mesh_bounding_box,
       model_object.m_raw_mesh_bounding_box_valid,
       model_object.cut_connectors,
       model_object.cut_id,
       model_object.layer_config_ranges);

    save_separate_chunk_with_version(
        ar,
        model_object.layer_height_profile,
        model_object.layer_height_profile.id().id,
        model_object.layer_height_profile.timestamp()
    );
}

template <class Archive>
struct specialize<
    Archive,
    Slic3r::Domain::ModelObject,
    cereal::specialization::non_member_load_save>
{};

template <class Archive>
void save(Archive& ar, const Slic3r::Domain::Model& model)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model));
    ar(model.objects.size());
    for (Slic3r::Domain::ModelObject* object : model.objects) {
        save_separate_chunk(ar, *object, object->id().id);
    }
}

template <class Archive>
void load(Archive& ar, Slic3r::Domain::Model& model)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&model));
    std::size_t size{};
    ar(size);
    model.clear_objects();
    for (std::size_t _{}; _ < size; ++_) {
        Slic3r::Domain::ModelObject* object{new Slic3r::Domain::ModelObject()};
        model.objects.push_back(object);
        load_separate_chunk(ar, *object);
    }
}

template <class Archive>
struct specialize<Archive, Slic3r::Domain::Model, specialization::non_member_load_save>
{};

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ObjectWithTimestamp& object)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectBase>(&object), object.m_timestamp);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::TriangleSelector::TriangleBitStreamMapping& mapping)
{
    ar(mapping.triangle_idx, mapping.bitstream_start_idx);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::TriangleSelector::TriangleSplittingData& data)
{
    ar(data.triangles_to_split, data.bitstream, data.used_states);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::FacetsAnnotation& annotation)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectWithTimestamp>(&annotation),
       annotation.triangle_splitting_data);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ZHeightPair& pair)
{
    ar(pair.layer_height, pair.z);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::LayerHeightProfile& profile)
{
    ar(cereal::base_class<Slic3r::Domain::ObjectWithTimestamp>(&profile), profile.m_data);
}

} // namespace cereal

namespace Slic3r::App::Undo {
SerializedData serialize_model(const Domain::Model& value, const SerializedData& previous_snapshot)
{
    SerializedData snapshot;
    SerializationContext context{snapshot, previous_snapshot};
    using OutputArchive =
        cereal::UserDataAdapter<SerializationContext, cereal::BinaryOutputArchive>;

    std::ostringstream oss;
    OutputArchive archive{context, oss};

    // Modifies chunk in the process!
    archive(value);

    snapshot.serialized_data = oss.str();

    return snapshot;
}

Domain::Model load_serialized_model(const SerializedData& data)
{
    using InputArchive = cereal::UserDataAdapter<SerializedData, cereal::BinaryInputArchive>;
    std::istringstream iss(data.serialized_data);

    // InputArchive must not modify chunk! Doing so is undefined behavior
    // because of this const cast. Sadly, ceral archive stores a non
    // const reference for the user data.
    InputArchive archive(const_cast<SerializedData&>(data), iss);

    Domain::Model result;
    archive(result);
    return result;
}
} // namespace Slic3r::App::Undo
