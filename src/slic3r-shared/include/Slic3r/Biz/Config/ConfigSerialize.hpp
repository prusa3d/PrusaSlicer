#pragma once

#include <string>
#include <variant>

#include "nlohmann/json_fwd.hpp"

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

namespace Slic3r::Domain {
void to_json(nlohmann::ordered_json& json_value, const Domain::BoxOrBoxesVector& box_or_boxes_vector);
void to_json(nlohmann::ordered_json& json_value, const Domain::ConfigBox& box);
}

namespace Slic3r::Biz::Config {

/// The project file form of a config pack. It is as_boxes(), except that each macro overrides
/// layer is an object with "name", "params" and "overrides", so the print mode that produced the
/// layer can be restored. Loaded back by load().
nlohmann::ordered_json to_json(const Domain::ConfigPackFDM& config_pack);
nlohmann::ordered_json to_json(const Domain::ConfigPackSLA& config_pack);

} // namespace Slic3r::Biz::Config

namespace Slic3r::Biz {

// Returns serialized value (or values) of the config option.
std::variant<std::string, std::vector<std::string>> value_as_string(const Domain::ConfigItem& item);

// Serialize given json into a single pretty string.
std::string beautify_json(const nlohmann::ordered_json& input, int indent, int squash_factor = 10);
std::string beautify_json(const Domain::BoxOrBoxesVector& box_or_boxes_vector, int indent);

} // namespace Slic3r::Biz
