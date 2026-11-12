#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"

namespace Slic3r::Biz::Config {

/**
 * @brief Inflates flat key values in format `<prefix>.nested.name: value` into JSON of
 * format `{ nested: { name: value }}`.
 *
 * The first format (flat key values) is useful for presets,  as it allows to define
 * fine-grained scope of data to be overridden. The JSON is used as interchange data format
 * with printer or connect.
 */
Domain::JsonValue features_to_structure(
    const Domain::Preset::FeatureValueMap& features,
    const std::string& key_prefix
);

void remove_structurize_features(Domain::Preset::FeatureValueMap& features);

/**
* @brief Deflates JSON of format `{ nested: { name: value }}` into flat key values
* in format `<prefix>.nested.name: value`.
*/
Domain::Preset::FeatureValueMap
structure_to_features(const Domain::JsonValue& features, const std::string& key_prefix);


}
