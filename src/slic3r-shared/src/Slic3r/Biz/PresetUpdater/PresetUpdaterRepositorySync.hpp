#pragma once

#include "Slic3r/Semver.hpp"

#include <set>
#include <vector>
#include <string>

namespace boost::filesystem {
class path;
} // namespace boost::filesystem

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterIndex;
class AbstractPresetUpdaterRepository;
class StagingArea;

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
     * @brief Records what every source offers, by staging its indices into update_sync.
     *
     * Downloads one index archive per online repository and nothing else. Vendor data is large
     * and most of it is never installed, so it is fetched by install_vendor instead, once the
     * user has asked for it.
     *
     * @param online_allowed when false, nothing is fetched and what is staged for online
     * repositories is replaced by the indices the installation carries, so an index an earlier
     * online check staged cannot outlive it. Local repositories are staged as usual.
     *
     * A private repository gets the same replacement while nobody is logged in: its offer is
     * known to be empty, so nothing is fetched, nothing is staged and nothing is reported.
     * What is installed from it keeps working and stays removable, from its installed indices.
     */
    void sync_indices(
        const SharedRepositoryVector& repositories,
        bool online_allowed,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * @brief Drops staged files of sources the database no longer knows.
     *
     * They accumulate when a source disappears from the server or another application version
     * staged for sources this one has never heard of. No job of any instance can be using them,
     * because every job works on sources its database knows.
     */
    void remove_unknown_sources(
        const std::set<std::string>& known_repo_ids,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * @brief Puts one vendor version in place, downloading it first unless it ships with the app.
     *
     * Nothing installed is touched until the new version is known to be complete, so a download
     * that fails or is cancelled leaves the installed presets as they were. Whatever was
     * downloaded is dropped again, installed or not.
     *
     * @param target_version the version the staged index recommends.
     * @param online_allowed when false, a version that has to be downloaded is refused outright
     * rather than attempted and retried. What the installation carries is installed either way.
     * @param staging the download space of the job this install belongs to.
     * @return false when nothing was installed, in which case process_status carries the reason.
     */
    bool install_vendor(
        const AbstractPresetUpdaterRepository* repo,
        const std::string& vendor_id,
        const Slic3r::Semver& target_version,
        bool online_allowed,
        const StagingArea& staging,
        PresetUpdaterProcessStatus* process_status
    ) const;

private:
    /**
     * @brief Stages the indices the installation carries, for repositories that have them.
     */
    void stage_indices_from_resources(
        const boost::filesystem::path& source_dir,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * @brief Downloads the index archive of one repository and stages every index in it.
     */
    void stage_indices_from_repository(
        const boost::filesystem::path& temp_path,
        const AbstractPresetUpdaterRepository* repo,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * @brief Installs from the data the application was shipped with. Needs no network.
     */
    bool install_from_resources(
        const AbstractPresetUpdaterRepository* repo,
        const std::string& vendor_id,
        const Slic3r::Semver& target_version,
        PresetUpdaterProcessStatus* process_status
    ) const;

    /**
     * @brief Downloads whatever the installed version does not already provide, then installs.
     */
    bool download_and_install(
        const AbstractPresetUpdaterRepository* repo,
        const std::string& vendor_id,
        const Slic3r::Semver& target_version,
        const boost::filesystem::path& staging_dir,
        PresetUpdaterProcessStatus* process_status
    ) const;
};

} // namespace Slic3r::Biz::PresetUpdater
