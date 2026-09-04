#include "Slic3r/Domain/GCodeMetadata.hpp"

#include <nlohmann/json.hpp>

#include "Slic3r/Biz/ProjectMetadataJson.hpp"
#include "Slic3r/Biz/Config/HwConfigJson.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

namespace Slic3r::Domain {

void to_json(nlohmann::ordered_json& j, const GCodeMetadata::General& v)
{
    j["producer"]         = v.producer;
    j["producer_version"] = v.producer_version;
    j["time"]             = v.time;
}

void to_json(nlohmann::ordered_json& j, const GCodeMetadata::Presets& v)
{
    j["vendor"]  = v.vendor;
    j["version"] = v.version;
    j["repo_id"] = v.repo_id;
}

void to_json(nlohmann::ordered_json& j, const GCodeMetadata::Config& v)
{
    j["presets"] = v.presets;
    j["printer"] = v.printer;
}

void to_json(nlohmann::ordered_json& j, const GCodeMetadata::Stats& stats)
{
    for (const auto& [k, v] : stats)
        std::visit([&j, &k]<typename T>(const T& tv) { j[k] = tv; }, v);
}

void to_json(nlohmann::ordered_json& j, const GCodeMetadata& v)
{
    j["general"] = v.general;
    j["config"]  = v.config;
    j["presets"] = std::visit(
        [](const auto& pack) -> BoxOrBoxesVector { return Domain::as_boxes(pack); },
        v.presets
    );
    j["project"] = v.project;
    j["stats"]   = v.stats;
}

} // namespace Slic3r::Domain
