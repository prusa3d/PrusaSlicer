#include "Slic3r/Biz/Config/FeatureStructurizer.hpp"

#include <string_view>
#include <algorithm>
#include <ranges>
#include <fmt/format.h>

namespace Slic3r::Biz::Config {

namespace {
void push_key_value(Domain::JsonObject& dest, std::string_view key, const Domain::JsonValue& value)
{
    auto idx = key.find('.');
    if (idx != std::string_view::npos) {
        std::string current_key{key.substr(0, idx)};
        if (!dest.contains(current_key)) {
            dest[current_key] = Domain::JsonObject{};
        }
        push_key_value(
            std::get<Domain::JsonObject>(dest[current_key]),
            key.substr(current_key.length() + 1),
            value
        );
    } else {
        dest[std::string{key}] = value;
    }
}

/**
 * @brief Pull key/value pairs from @a values JSONObject to @a dest FeatureValueMap
 * @param dest Destination to store data to
 * @param key_prefix Complete key prefix to put in front of key
 * @param prefix_to_preserve First part of prefix to preserve
 * @param prefix_to_strip Second part of prefix to strip
 * @param values Source values
 */
void pull_key_values(
    Domain::Preset::FeatureValueMap& dest,
    const std::string& key_prefix,
    const std::string& prefix_to_preserve,
    const std::string& prefix_to_strip,
    const Domain::JsonObject& values
)
{
    for (const auto& [k, v] : values) {
        std::string prefixed_key = key_prefix + k;
        if (std::holds_alternative<Domain::JsonObject>(v)) {
            pull_key_values(
                dest,
                prefixed_key + ".",
                prefix_to_preserve,
                prefix_to_strip,
                std::get<Domain::JsonObject>(v)
            );
        } else if (prefixed_key.starts_with(prefix_to_preserve + prefix_to_strip)) {
            std::string real_key = prefixed_key.substr(0, prefix_to_preserve.length())
                + prefixed_key.substr(prefix_to_preserve.length() + prefix_to_strip.length());
            dest[real_key] = v;
        }
    }
}

const std::string STRUCTURIZE_PREFIX = "$.";

} // namespace


Domain::JsonValue features_to_structure(
    const Domain::Preset::FeatureValueMap& features,
    const std::string& key_prefix
)
{
    Domain::JsonObject ret;
    for (const auto& [k, v] : features) {
        if (!k.starts_with(STRUCTURIZE_PREFIX))
            continue;
        push_key_value(ret, key_prefix + k.substr(STRUCTURIZE_PREFIX.length()), v);
    }
    return ret;
}

Domain::Preset::FeatureValueMap
structure_to_features(const Domain::JsonValue& features, const std::string& key_prefix)
{
    Domain::Preset::FeatureValueMap ret;
    pull_key_values(
        ret,
        STRUCTURIZE_PREFIX,
        STRUCTURIZE_PREFIX,
        key_prefix,
        std::get<Domain::JsonObject>(features)
    );

    return ret;
}

void remove_structurize_features(Domain::Preset::FeatureValueMap& features)
{
    std::erase_if(
        features,
        [](const auto& kv) { return kv.first.starts_with(STRUCTURIZE_PREFIX); }
    );
}




}
