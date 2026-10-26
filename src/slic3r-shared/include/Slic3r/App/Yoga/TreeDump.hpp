#pragma once

#include <nlohmann/json.hpp>

namespace Slic3r::App::Yoga {

class Object;
class RootItem;

nlohmann::json dump_node(const Object& object, bool include_hidden = false);
nlohmann::json dump_tree(const RootItem& root, bool include_hidden = false);

} // namespace Slic3r::App::Yoga
