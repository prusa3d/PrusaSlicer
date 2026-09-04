#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterJob.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"
#include "Slic3r/Biz/PresetUpdater/IPresetUpdaterResultListener.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <jthread/JThread.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace Slic3r::Biz::PresetUpdater {

/**
 * @brief Runs preset updater operations one at a time, in submission order.
 *
 * Every public operation enqueues a job and returns its JobId immediately; the caller is never
 * blocked. Preset updater jobs all mutate the same on-disk state, so exactly one may run at a
 * time and none may be dropped.
 *
 * Results are dispatched to IPresetUpdaterResultListener on the main thread and each JobId is
 * terminated by exactly one on_preset_updater_job_finished. After shutdown(), or once the
 * dispatcher is closed, the queue is dropped and every operation refuses with k_invalid_job_id.
 *
 * All public methods must be called from the main thread.
 */
class PresetUpdaterInteractor : public WithListeners<IPresetUpdaterResultListener>
{
public:
    PresetUpdaterInteractor(Platform::IMainThreadDispatcher& dispatcher);
    ~PresetUpdaterInteractor();

    /**
     * @brief Only checks existing installed files against app version.
     * Success is on_preset_updater_forced_reconfigurations_list.
     */
    JobId check_forced_reconfigurations();

    /**
     * @brief Does full construction of update_sync folder and checks reconfigurations.
     * Success is on_preset_updater_reconfigurations_list.
     * @param online_allowed when false the job touches no network at all: the source list is taken
     * from the data dir and online sources are left with whatever is already staged for them. What
     * comes with the installation and what local sources offer is staged and checked either way.
     * @param ignore_hash should be true ONLY for testing purpose.
     * @param source_list fetches the repository manifest from the server first. Pass UseStored to
     * check against the source list already in the data dir, which needs no network - the listing
     * or selection this check follows has just written it.
     * @param required_repo_ids sources to stage and check on top of the selected ones, because a
     * forced reconfiguration names them and its presets stay unusable until they are updated.
     */
    JobId build_update_sync_and_reconfiguration_check(
        bool online_allowed,
        VerboseStyle verbose,
        bool ignore_hash                              = false,
        SourceListSync source_list                    = SourceListSync::Fetch,
        const std::set<std::string>& required_repo_ids = {}
    );

    /**
     * @brief Performs the selected buckets of a ReconfigurationList.
     * Success is on_preset_updater_reconfigurations_performed.
     */
    JobId perform_reconfigurations(
        const PresetUpdaterReconfigurationList& reconfigurations,
        ReconfigurationType types_to_perform = ReconfigurationType::All
    );

    /**
     * @brief Stores the given selection of source repositories in the app manifest file.
     * Success is on_preset_updater_repository_selection_performed.
     * Use list_repositories to query sources without changing the selection.
     */
    JobId apply_repository_selection(const SharedPresetUpdaterRepositoryInfoVector& repos);

    /**
     * @brief Adds local repository to app manifest file, unzipping it into the data dir.
     * Success is on_preset_updater_repository_info_vector. Any other repository sharing the id of
     * the added one is unselected, because only one repository per id may be selected at a time.
     */
    JobId add_local_repository(const boost::filesystem::path& zip_path);

    /**
     * @brief Removes local repository from app manifest file.
     * Success is on_preset_updater_repository_info_vector.
     */
    JobId remove_local_repository(const std::string& uuid);

    /**
     * @brief Lists all known source repositories.
     * Success is on_preset_updater_repository_info_vector.
     * @param online_allowed when false the stored source list is used whatever force_sync says.
     * @param force_sync fetches the repository manifest from the server first. Pass false to list
     * what is already stored in the data dir, which needs no network.
     */
    JobId list_repositories(bool online_allowed, bool force_sync = true);

    JobId cleanup_update_sync();

    /**
     * @brief Cancels one job: a running one is asked to stop without blocking the caller, a queued
     * one is dropped. Jobs queued behind it still run. The stop is cooperative, so an operation
     * that has already written to the data dir reports it and finishes Succeeded instead.
     * @return false if the id is unknown, which includes an already finished job.
     */
    bool cancel_job(JobId job_id);

    /**
     * @brief Cancels the running job and drops everything queued behind it.
     */
    void cancel_all_jobs();

    void on_notification_cancel();

    /**
     * @brief Cancels the running job, waits for it to finish, and drops everything queued. Starts
     * nothing further, so it is safe at any time and may be called more than once. The destructor
     * calls it.
     */
    void shutdown();

    /**
     * @brief Number of jobs submitted and not yet finished, including the running one.
     */
    size_t pending_job_count() const;

    bool is_job_pending(JobId job_id) const;

    /**
     * @brief Name the running job is registered under in Platform::JobManager.
     */
    static std::string job_manager_name(JobId job_id);

private:
    /**
     * @brief Body of one queued job. Returns the state the job is to be finished with.
     * The signature is what Platform::JobManager expects: StopToken first, then ProgressTracker,
     * then the job's own arguments. JobId is taken by value so JobManager stores a copy.
     */
    using JobBody =
        std::function<JobState(JThread::StopToken, Platform::JobManager::ProgressTracker, JobId)>;

    struct PendingJob
    {
        JobId id{k_invalid_job_id};
        JobBody body;
    };

    bool accepts_work() const;
    JobId enqueue(JobBody body);
    void start_next();
    void finish_running(JobId job_id, JobState state);
    bool request_running_stop();

    /**
     * @brief Builds the status callback handed to PresetUpdaterProcessStatus. It reports to the
     * listeners as before and additionally feeds the job's ProgressTracker, so preset updater work
     * is visible in the global job status.
     */
    std::function<void(const std::string&, int, unsigned)> make_status_callback(
        JobId job_id, Platform::JobManager::ProgressTracker progress_tracker, VerboseStyle verbose
    );

    std::deque<PendingJob> m_queue;
    std::optional<JobId> m_running;
    JobId m_next_job_id{k_invalid_job_id + 1};
    bool m_shut_down{false};

    Platform::IMainThreadDispatcher& m_dispatcher;

    // Error - operation has failed
    void dispatch_error(JobId job_id, const std::string& body, PresetUpdaterReason reason);
    // Status - operation is ongoing
    void dispatch_status(JobId job_id, const std::string& target, int attempt, unsigned delay, VerboseStyle verbose);
    // Success - operation has finished
    void dispatch_forced_reconfigurations_list(JobId job_id, const PresetUpdaterReconfigurationList& reconfigurations, const std::vector<PresetUpdaterWarning>& warnings);
    void dispatch_reconfigurations_list(JobId job_id, const PresetUpdaterReconfigurationList& reconfigurations, const std::vector<PresetUpdaterWarning>& warnings, VerboseStyle verbose);
    void dispatch_reconfigurations_performed(JobId job_id, const std::vector<PresetUpdaterWarning>& warnings);
    void dispatch_repository_info_vector(JobId job_id, const SharedPresetUpdaterRepositoryInfoVector& descriptor, const std::vector<PresetUpdaterWarning>& warnings);
    void dispatch_repository_selection_performed(JobId job_id, const SharedPresetUpdaterRepositoryInfoVector& descriptor, const std::vector<PresetUpdaterWarning>& warnings);
    // Terminal - dispatched exactly once per job
    void dispatch_job_finished(JobId job_id, JobState state);
};

} // namespace Slic3r::Biz::PresetUpdater
