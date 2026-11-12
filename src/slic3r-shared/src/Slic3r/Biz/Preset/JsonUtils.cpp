#include "Slic3r/Biz/Preset/JsonUtils.hpp"
#include <nlohmann/json.hpp>

namespace {
void apply_nulls_recursively(nlohmann::json& j_base, const nlohmann::json& j_over) {
    for (auto& [key, val] : j_over.items()) {
        if (val.is_null()) {
            j_base[key] = nullptr;
        } else if (val.is_object() && j_base.contains(key) && j_base[key].is_object()) {
            apply_nulls_recursively(j_base[key], val);
        }
    }
}
}

namespace Slic3r::Biz::Preset {
std::string merge_json(const std::string& base_json, const std::string& override_json)
{
    if (base_json.empty())
        return override_json;
    if (override_json.empty())
        return base_json;

    using json = nlohmann::json;

    try {
        json j_base = json::parse(base_json);
        json j_over = json::parse(override_json);

        if (!j_base.is_object() || !j_over.is_object()) {
            return override_json;
        }

        j_base.merge_patch(j_over);

        // merge_patch has removed all keys which had a null override.
        // Recursively iterate through j_over and add them as nulls.
        apply_nulls_recursively(j_base, j_over);

        return j_base.dump();
    } catch (const json::parse_error&) {
        return override_json;
    }
}
} // namespace Slic3r::Biz::Preset
