#pragma once

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/App/Undo/SerializedData.hpp"

namespace Slic3r::App::Undo {
SerializedData serialize_model(
    const Domain::Model& value,
    const SerializedData& previous_snapshot
);

Domain::Model load_serialized_model(const SerializedData& data);

} // namespace Slic3r::Biz::Undo
