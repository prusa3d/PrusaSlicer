#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r {

void to_json(nlohmann::json& j, const Slic3r::Semver& v) {
    j = v.to_string();
}

void from_json(const nlohmann::json& j, Semver& v)
{
    if (!j.is_string()) {
        v = Semver::invalid();
        return;
    }
    const std::string str = j.get<std::string>();
    if (boost::optional<Semver> parsed = Semver::parse(str); parsed) {
        v = std::move(*parsed);
    } else {
        v = Semver::invalid();
    }
}

}

namespace Slic3r::Biz::PresetUpdater {

NLOHMANN_JSON_SERIALIZE_ENUM(VendorReconfigurationState, {
    {VendorReconfigurationState::Update, "Update"},
    {VendorReconfigurationState::ForcedUpdate, "ForcedUpdate"},
    {VendorReconfigurationState::ForcedDowngrade, "ForcedDowngrade"},
    {VendorReconfigurationState::NotInIndex, "NotInIndex"},
    {VendorReconfigurationState::NewVendor, "NewVendor"},
    {VendorReconfigurationState::RemoveVendor, "RemoveVendor"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    VendorReconfiguration,
    state,
    vendor_id,
    vendor_repo_id,
    current_version,
    recommended_version,
    comment
    //changelog_url,
    //new_printers
)

void to_json(nlohmann::json& j, const PresetUpdaterReconfigurationList& list)
{
    // Use the public accessors
    j = nlohmann::json{
        {"regular_updates", list.regular_updates()},
        {"forced_updates", list.forced_updates()},
        {"forced_downgrades", list.forced_downgrades()},
        {"not_in_index", list.not_in_index()},
        {"new_vendors", list.new_vendors()},
        {"removals", list.removals()}
    };
}

} // namespace Slic3r::Biz::PresetUpdater
