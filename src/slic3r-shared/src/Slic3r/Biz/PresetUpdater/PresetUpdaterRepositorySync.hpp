#pragma once

#include <memory>
#include <vector>
#include <map>
#include <string>

namespace boost::filesystem {
class path;
} // namespace boost::filesystem

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterIndex;
class AbstractPresetUpdaterRepository;

typedef std::vector<const AbstractPresetUpdaterRepository*> SharedRepositoryVector;

class PresetUpdaterProcessStatus;

class PresetUpdaterRepositorySync
{
public:
    PresetUpdaterRepositorySync()                                              = default;
    PresetUpdaterRepositorySync(PresetUpdaterRepositorySync&&)                 = delete;
    PresetUpdaterRepositorySync(const PresetUpdaterRepositorySync&)            = delete;
    PresetUpdaterRepositorySync& operator=(PresetUpdaterRepositorySync&&)      = delete;
    PresetUpdaterRepositorySync& operator=(const PresetUpdaterRepositorySync&) = delete;
    ~PresetUpdaterRepositorySync()                                             = default;

    /**
     * @brief Stages from resources for every repository, then from the repositories themselves.
     * @param online_allowed when false, online repositories are left untouched - what is already
     * staged for them stays, and nothing is fetched.
     */
    void sync(
        const SharedRepositoryVector& repositories,
        bool online_allowed,
        PresetUpdaterProcessStatus* process_status
    ) const;

private:
    /**
     * @brief Uses resources as source. It stages reconfigurations into update_sync.
     */
    void stage_rencofigurations_from_resources(
        const boost::filesystem::path& source_dir,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status
    ) const;

    void stage_not_installed_vendor_from_resources(
        const boost::filesystem::path& source_dir,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status,
        const PresetUpdaterIndex& source_index
    ) const;

    void stage_installed_vendor_from_resources(
        const boost::filesystem::path& source_dir,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status,
        const PresetUpdaterIndex& source_index
    ) const;

    /**
     * Downloads index archive to temp_path. For each index calls sync_not_installed_vendor or sync_installed_vendor.
     */
    void sync_repository(
        const boost::filesystem::path& temp_path,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * Downloads recommended bundle to temp_path / vendor_name. Then calls check_missing_resources.
     */
    void sync_not_installed_vendor(
        const boost::filesystem::path& temp_path,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status,
        const PresetUpdaterIndex& index
    ) const;

    /**
     * Compares version of installed bundle of vendor with index. Then downloads recommended bundle to temp_path / vendor_name if needed. Then calls check_missing_resources.
     */
    void sync_installed_vendor(
        const boost::filesystem::path& temp_path,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status,
        const PresetUpdaterIndex& index
    ) const;
};

} // namespace Slic3r::Biz::PresetUpdater
