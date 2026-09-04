#include "Slic3r/App/Undo/ToolStateSerialize.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

namespace cereal {

template <class Archive>
void serialize(Archive& ar, Slic3r::Biz::Cut::Groove& groove)
{
    ar(groove.depth,
       groove.width,
       groove.flaps_angle,
       groove.angle,
       groove.depth_init,
       groove.width_init,
       groove.flaps_angle_init,
       groove.angle_init,
       groove.depth_tolerance,
       groove.width_tolerance);
}

template <class Archive, Slic3r::Domain::BoundingBoxConcept BoxType>
void serialize(Archive& archive, BoxType& box)
{
    archive(box.min, box.max, box.defined);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::App::Undo::CutGizmoState::PartState& part_state)
{
    archive(part_state.keep, part_state.place_on_cut, part_state.flip);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::App::Undo::CutGizmoState& tool_state)
{
    ar(tool_state.bounding_box,
       tool_state.is_planar_mode,
       tool_state.connectors_editing,
       tool_state.center_offset,
       tool_state.rotation_m,
       tool_state.keep_as_parts,
       tool_state.part_state_upper,
       tool_state.part_state_lower,
       tool_state.groove);
}

template <class Archive>
void serialize(Archive& ar, Slic3r::App::Undo::HeightRangeGizmoState& tool_state)
{
    ar(tool_state.selected_height_range);
}

} // namespace cereal

namespace Slic3r::App::Undo {

SerializedData serialize_tools_state(const ToolsState& tools_state)
{
    std::ostringstream oss;
    cereal::BinaryOutputArchive archive{oss};
    archive(tools_state);

    SerializedData snapshot;
    snapshot.serialized_data = oss.str();
    return snapshot;
}

ToolsState load_serialized_tools_state(const SerializedData& data)
{
    std::istringstream iss(data.serialized_data);
    cereal::BinaryInputArchive archive(iss);

    ToolsState result;
    archive(result);
    return result;
}

} // namespace Slic3r::App::Undo
