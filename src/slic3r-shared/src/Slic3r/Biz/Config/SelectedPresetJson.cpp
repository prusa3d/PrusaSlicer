#include "Slic3r/Biz/Config/SelectedPresetJson.hpp"
#include "Slic3r/Biz/Config/HwConfigJson.hpp"
#include "Slic3r/Biz/JsonValueJson.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"
#include <nlohmann/json.hpp>
#include <yaml-cpp/exceptions.h>

namespace Slic3r::Domain::Expr {

void from_json(const nlohmann::ordered_json& j, ExprAst& v)
{
    v = Biz::Expr::Parser().parse(j.get<std::string>());
}

void to_json(nlohmann::ordered_json& j, const ExprAst& v)
{
    j = to_string(v);
}

} // namespace Slic3r::Domain::Expr

namespace Slic3r::Domain::Preset {

void from_json(const nlohmann::ordered_json& j, EvaluatedPresetMetadata& v)
{
    v.name       = j["name"].get<std::string>();
    v.id         = j["id"].get<std::string>();
    v.root_id    = j["root_id"].get<std::string>();
    v.conditions = j["conditions"].get<std::vector<std::string>>();
    if (j.contains("features")) {
        v.features = j["features"].get<FeatureValueMap>();
    }
}

void to_json(nlohmann::ordered_json& j, const EvaluatedPresetMetadata& v)
{
    j["name"]       = v.name;
    j["id"]         = v.id;
    j["root_id"]    = v.root_id;
    j["conditions"] = v.conditions;
    if (!v.features.empty()) {
        j["features"] = v.features;
    }
}

void to_json(nlohmann::ordered_json& j, const SelectedPresetMetadata& v)
{
    j["hw_config"] = v.hw_config;
    j["printer"]   = v.printer;
    j["print"]     = v.print;
    j["tools"]     = v.tools;
    j["materials"] = v.materials;
}

} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Config {

tl::expected<Domain::Preset::EvaluatedPresetMetadata, std::string> load_evaluated_preset_metadata(
    const nlohmann::ordered_json& j
)
{
    try {
        return j.get<Domain::Preset::EvaluatedPresetMetadata>();
    } catch (const Biz::Expr::ParseError& e) {
        return tl::unexpected{e.what()};
    }
}

tl::expected<Domain::Preset::SelectedPresetMetadata, std::string> load_preset_metadata(
    const nlohmann::ordered_json& j
)
{
    Domain::Preset::SelectedPresetMetadata ret;
    if (auto res = load_hw_config(j.at("hw_config")); !res) {
        return tl::unexpected{res.error()};
    } else {
        ret.hw_config = res.value();
    }

    if (auto res = load_evaluated_preset_metadata(j.at("printer")); !res) {
        return tl::unexpected{res.error()};
    } else {
        ret.printer = res.value();
    }

    if (auto res = load_evaluated_preset_metadata(j.at("print")); !res) {
        return tl::unexpected{res.error()};
    } else {
        ret.print = res.value();
    }

    for (const auto& t : j.at("tools")) {
        if (auto res = load_evaluated_preset_metadata(t); !res) {
            return tl::unexpected{res.error()};
        } else {
            ret.tools.emplace_back(std::move(res.value()));
        }
    }

    for (const auto& t : j.at("materials")) {
        if (auto res = load_evaluated_preset_metadata(t); !res) {
            return tl::unexpected{res.error()};
        } else {
            ret.materials.emplace_back(std::move(res.value()));
        }
    }
    return ret;
}

} // namespace Slic3r::Biz::Config
