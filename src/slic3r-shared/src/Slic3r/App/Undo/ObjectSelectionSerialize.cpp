#include "Slic3r/App/Undo/ObjectSelectionSerialize.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

namespace cereal {

template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::ElementRef& ref)
{
    ar(ref.object_id,
       ref.instance_id,
       ref.volume_id,
       ref.wipe_tower_id.project_id,
       ref.wipe_tower_id.bed_instance_id);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::Biz::Scene::ObjectSelection& selection)
{
    ar(selection.mode, selection.elements);
}

} // namespace cereal

namespace Slic3r::App::Undo {
SerializedData serialize_object_selection(const Biz::Scene::ObjectSelection& value)
{
    std::ostringstream oss;
    cereal::BinaryOutputArchive archive{oss};

    archive(value);

    SerializedData snapshot;
    snapshot.serialized_data = oss.str();

    return snapshot;
}

Biz::Scene::ObjectSelection load_serialized_object_selection(const SerializedData& data)
{
    std::istringstream iss(data.serialized_data);
    cereal::BinaryInputArchive archive(iss);

    Biz::Scene::ObjectSelection result;
    archive(result);
    return result;
}

} // namespace Slic3r::Biz::Undo
