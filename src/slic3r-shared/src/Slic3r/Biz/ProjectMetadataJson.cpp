#include "Slic3r/Biz/ProjectMetadataJson.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::Domain {

void to_json(nlohmann::ordered_json& j, const ProjectMetadata& v)
{
    j = nlohmann::ordered_json{{"id", v.id}, {"version", v.version}};
}


void from_json(const nlohmann::ordered_json& j, ProjectMetadata& v)
{
    j.at("id").get_to(v.id);
    j.at("version").get_to(v.version);
}

} // namespace Slic3r::Domain

