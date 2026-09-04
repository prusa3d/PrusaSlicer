#pragma once

#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/App/Undo/SerializedData.hpp"

namespace Slic3r::App::Undo {
SerializedData serialize_object_selection(const Biz::Scene::ObjectSelection& value);

Biz::Scene::ObjectSelection load_serialized_object_selection(const SerializedData& data);

} // namespace Slic3r::Biz::Undo
