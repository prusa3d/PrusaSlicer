#include "Slic3r/Biz/Preset/PresetMatchRuleFactor.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/TypeInfo.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"

namespace Slic3r::Biz::Preset {

namespace {

bool is_required_tag(const std::string& tag)
{
    return tag == "abrasive";
}

}

std::optional<Domain::Preset::MatDbMatchRule> make_match_rule(const Domain::Preset::FeatureValueMap& features)
{
    Domain::Preset::MatDbMatchRule rule = {};

    bool inited = false;
    auto try_append = [&features, &inited]<typename T>(T& dest, const char* key) {
        if (auto it = features.find(key); it != features.end()) {
            if constexpr (Domain::is_std_optional_v<T>){
                using VT = T::value_type;
                const auto* val = std::get_if<VT>(&it->second);
                if (val != nullptr) {
                    dest = *val;
                    inited = true;
                } else {
                    SPDLOG_ERROR("Expected type for {} is {}, but wasn't found", key, type_name<T>());
                }
            } else if constexpr (Domain::is_std_vector_v<T>) {
                const auto* arr = std::get_if<Domain::JsonArray>(&it->second);
                if (arr != nullptr) {
                    T parsed_val;
                    parsed_val.reserve(arr->size());
                    for (const auto& v : *arr) {
                        const typename T::value_type* parsed_el = std::get_if<typename T::value_type>(&v);
                        if (parsed_el == nullptr) {
                            SPDLOG_ERROR(
                                "Expected type for {} is array of {}, but wasn't found",
                                key,
                                type_name<typename T::value_type>()
                            );
                            return;
                        }
                        parsed_val.emplace_back(std::move(*parsed_el));
                    }
                    dest = std::move(parsed_val);
                    inited = true;
                } else {
                    SPDLOG_ERROR(
                        "Expected type for {} elements is {}, but wasn't found",
                        key,
                        type_name<typename T::value_type>()
                    );
                }
            } else {
                static_assert("Unsupported type");
            }
        }
    };


    try_append(rule.uuid, "$.uuid");
    try_append(rule.slug, "$.slug");
    try_append(rule.klass, "$.class");
    try_append(rule.type, "$.type");
    try_append(rule.abbreviation, "$.abbreviation");
    try_append(rule.brand_slug, "$.brand.slug");
    Domain::MatDb::StringList tags;
    try_append(tags, "$.tags");
    for (const auto& tag: tags) {
        (is_required_tag(tag) ? rule.required_tags : rule.optional_tags).push_back(tag);
   }

    if (inited) {
        return rule;
    }
    return std::nullopt;
}

} // namespace Slic3r::Biz::Preset
