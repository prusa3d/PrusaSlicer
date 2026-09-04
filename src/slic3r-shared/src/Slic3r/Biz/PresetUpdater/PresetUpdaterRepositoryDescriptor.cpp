#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"

#include <nlohmann/json.hpp>

namespace nlohmann {
template <>
struct adl_serializer<boost::filesystem::path> {
    static void to_json(json& j, const boost::filesystem::path& p) {
        j = p.generic_string();
    }

    static void from_json(const json& j, boost::filesystem::path& p) {
        p = j.get<std::string>();
    }
};
} // namespace nlohmann

namespace Slic3r::Biz::PresetUpdater {

void to_json(nlohmann::json& j, const PresetUpdaterRepositoryDescriptor& d) {
    j = nlohmann::json{
        {"id", d.id},
        {"name", d.name},
        {"url", d.url},
        {"uuid", d.uuid},
        {"index_url", d.index_url},
        {"description", d.description},
        {"visibility", d.visibility},
        {"offline_archive_url", d.offline_archive_url},
        {"unzipped_data_path", d.unzipped_data_path},
        {"zip_path", d.zip_path}
    };
}

void from_json(const nlohmann::json& j, PresetUpdaterRepositoryDescriptor& d) {
    j.at("id").get_to(d.id);
    j.at("name").get_to(d.name);
    j.at("url").get_to(d.url);
    j.at("uuuid").get_to(d.uuid);
    d.index_url = j.value("index_url", "");
    d.description = j.value("description", "");
    d.visibility = j.value("visibility", "");
    d.offline_archive_url = j.value("offline_archive_url", "");
    d.unzipped_data_path = j.value("unzipped_data_path", "");
    d.zip_path = j.value("zip_path", "");
}

void to_json(nlohmann::json& j, const SharedPresetUpdaterRepositoryInfo& i) {
    j = nlohmann::json{
        {"descriptor", i.descriptor},
        {"selected", i.selected}
    };
}

} // namespace Slic3r::Biz::PresetUpdater