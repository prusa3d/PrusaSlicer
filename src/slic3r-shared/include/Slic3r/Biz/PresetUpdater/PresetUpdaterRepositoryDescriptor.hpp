#pragma once

#include <string>
#include <string_view>
#include <boost/filesystem/path.hpp>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Biz::PresetUpdater {

enum class VerboseStyle{
    ProgressNotification,
    NoProgress
};

enum class SourceListSync
{
    Fetch,
    UseStored
};

/// name for UI layer
inline constexpr std::string_view k_manifest_download_target{"Sources Database Manifest"};

struct PresetUpdaterRepositoryDescriptor
{
    // mandatory
    std::string id;
    std::string name;
    std::string url;
    // mandatory in runtime but optional in manifest
    std::string uuid;
    // optional
    std::string index_url;
    std::string description;
    std::string visibility;
    std::string offline_archive_url;
    // not read from manifest json
    boost::filesystem::path unzipped_data_path; // Where archive is unzipped. Created each app run.
    boost::filesystem::path zip_path; // Path given by user. Stored between app runs.

    PresetUpdaterRepositoryDescriptor() = default;

    PresetUpdaterRepositoryDescriptor(
        const std::string& id,
        const std::string& name,
        const std::string& url,
        const std::string& uuid,
        const std::string& index_url               = "",
        const std::string& description             = "",
        const std::string& visibility              = "",
        const boost::filesystem::path& tmp_path    = "",
        const boost::filesystem::path& source_path = ""
    ) :
        id(id),
        name(name),
        url(url),
        uuid(uuid),
        index_url(index_url),
        description(description),
        visibility(visibility),
        unzipped_data_path(tmp_path),
        zip_path(source_path)
    {}

    PresetUpdaterRepositoryDescriptor(const PresetUpdaterRepositoryDescriptor&)     = default;
    PresetUpdaterRepositoryDescriptor(PresetUpdaterRepositoryDescriptor&&) noexcept = default;
    PresetUpdaterRepositoryDescriptor& operator=(const PresetUpdaterRepositoryDescriptor&) = default;
    PresetUpdaterRepositoryDescriptor& operator=(PresetUpdaterRepositoryDescriptor&&) noexcept = default;
};

struct SharedPresetUpdaterRepositoryInfo
{
    PresetUpdaterRepositoryDescriptor descriptor;
    bool selected;
};

// This are readonly creadentials of all repositories with setable bool - selected by user.
// This is all UI should need to construct Repository Sources Selection.
typedef std::vector<SharedPresetUpdaterRepositoryInfo> SharedPresetUpdaterRepositoryInfoVector;

void to_json(nlohmann::json& j, const SharedPresetUpdaterRepositoryInfo& i);

} // namespace Slic3r::Biz::PresetUpdater