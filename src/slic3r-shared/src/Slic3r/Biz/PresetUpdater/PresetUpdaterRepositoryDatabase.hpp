#pragma once

#include "PresetUpdaterRepository.hpp"

#include <boost/uuid/uuid_generators.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterProcessStatus;

typedef std::vector<std::unique_ptr<AbstractPresetUpdaterRepository>> PrivateRepositoryVector;
typedef std::vector<const AbstractPresetUpdaterRepository*> SharedRepositoryVector;

class PresetUpdaterRepositoryDatabase
{
public:
    /**
     * @brief Constructor does loads manifest in datadir.
     */
    PresetUpdaterRepositoryDatabase(PresetUpdaterProcessStatus* process_status);
    PresetUpdaterRepositoryDatabase(PresetUpdaterRepositoryDatabase&&)                 = delete;
    PresetUpdaterRepositoryDatabase(const PresetUpdaterRepositoryDatabase&)            = delete;
    PresetUpdaterRepositoryDatabase& operator=(PresetUpdaterRepositoryDatabase&&)      = delete;
    PresetUpdaterRepositoryDatabase& operator=(const PresetUpdaterRepositoryDatabase&) = delete;
    ~PresetUpdaterRepositoryDatabase();

    /**
     * @brief Performs GET for online repo manifest. Then loads it, merges with current data (loaded in constructor) and stores into manifest in datadir.
     * Returns true if successfully got the data.
     */
    bool sync(PresetUpdaterProcessStatus* process_status);

    /**
     * @brief Returns Vector with all repository descriptor, uuid and selected flag.
     * Its all Frontend needs to show repositories to user.
     */
    SharedPresetUpdaterRepositoryInfoVector get_all_repositories() const;

    /**
     * @brief Returns const pointers to selected repositories. Used by prepare update_sync and perform reconfigurations
     */
    SharedRepositoryVector get_selected_repositories() const;

    /**
     * @brief The selected repositories, plus one repository for each required id that has none
     * selected. Never more than one repository per id, because everything downstream addresses a
     * repository by its id alone. A required id no repository offers is left out.
     */
    SharedRepositoryVector get_selected_and_required_repositories(
        const std::set<std::string>& required_ids
    ) const;

    /**
     * @brief Changes "selected" flag on repositories.
     */
    void apply_selection(
        const SharedPresetUpdaterRepositoryInfoVector& repos,
        PresetUpdaterProcessStatus* process_status
    );

    /**
     * @brief Creates new local repository, unzips it to its newly created directory.
     * Unzipped data lives in datadir until removal of local repo.
     * Other repositories sharing the id of the added one are unselected, because only one
     * repository per id may be selected at a time.
     */
    void add_local_repository(
        const boost::filesystem::path& zip_path,
        PresetUpdaterProcessStatus* process_status
    );

    /**
     * @brief Removes local repository, deletes its data directory.
     * If the removed repository was the selected one for its id, the online repository offering
     * that id is selected in its place, so the id is not left without a source.
     */
    void remove_local_repository(const std::string& uuid, PresetUpdaterProcessStatus* process_status);

private:
    /**
     * @brief Reads manifest file in data dir or creates it from resources. Called from constructor. Fills repository vectors and maps. \
     */
    void load_app_manifest_json(PresetUpdaterProcessStatus* process_status);

    /**
     * @brief Updates repositories over manifest that was downloaded in sync(). Called from sync().
     */
    void read_server_manifest(const std::string& json_body, PresetUpdaterProcessStatus* process_status);

    /**
     * @brief Rewrites manifest file in datadir with current repos.
     */
    void save_app_manifest_json(PresetUpdaterProcessStatus* process_status) const;

    /**
     * @brief Deletes all data in destination folder, that are not part of repo vector.
     * A local repository is unzipped before it is written to the manifest, so a job that fails
     * in between leaves an unzipped folder behind that belongs to no repository. Call this only
     * once the repo vector is the one to keep, or the data of a pending repository is deleted too.
     */
    void consolidate_offline_repo_unzipped_folders(PresetUpdaterProcessStatus* process_status) const;

    /**
     * @brief Selects the online repository offering the given id, unless some repository with that
     * id is already selected.
     */
    void select_online_repository_with_id(const std::string& id);

    // Helper methods
    void copy_initial_manifest(PresetUpdaterProcessStatus* process_status) const;
    void clear_online_repos();
    
    boost::filesystem::path get_stored_manifest_path() const;
    boost::filesystem::path m_unq_tmp_path;
    PrivateRepositoryVector m_all_repositories;
    
    static std::string get_next_uuid();
};

} // namespace Slic3r::Biz::PresetUpdater
