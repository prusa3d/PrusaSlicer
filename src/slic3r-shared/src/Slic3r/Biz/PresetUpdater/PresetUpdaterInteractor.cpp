#include "Slic3r/Biz/PresetUpdater/PresetUpdaterInteractor.hpp"
#include "PresetUpdaterUtils.hpp"
#include "PresetUpdaterProcessStatus.hpp"
#include "PresetUpdaterRepositoryDatabase.hpp"
#include "PresetUpdaterRepositorySync.hpp"

#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace Slic3r::Biz::PresetUpdater {

namespace {

Platform::JobManager::JobManager* job_manager_or_null()
{
    Platform::PlatformServices& services = Platform::PlatformServices::instance();
    return services.has_job_manager() ? &services.job_manager() : nullptr;
}

std::string exception_message(const std::exception_ptr& exception)
{
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& e) {
        return std::string("PresetUpdater operation failed with an unexpected exception: ") + e.what();
    } catch (...) {
        return "PresetUpdater operation failed with an unknown exception.";
    }
}

} // namespace

PresetUpdaterInteractor::PresetUpdaterInteractor(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher(dispatcher)
{}

PresetUpdaterInteractor::~PresetUpdaterInteractor()
{
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );

    shutdown();
}

void PresetUpdaterInteractor::shutdown()
{
    m_shut_down = true;
    m_queue.clear();
    // The running job body holds this and is still touching the data directory. 
    // cancel_job blocks, which is what we want here.
    if (m_running.has_value()) {
        if (Platform::JobManager::JobManager* jobs = job_manager_or_null()) {
            jobs->cancel_job(job_manager_name(*m_running));
        }
        m_running.reset();
    }
}

std::string PresetUpdaterInteractor::job_manager_name(JobId job_id)
{
    return "preset_updater/" + std::to_string(job_id);
}

bool PresetUpdaterInteractor::accepts_work() const
{
    return !m_shut_down && !m_dispatcher.is_closed();
}

JobId PresetUpdaterInteractor::enqueue(JobBody body)
{
    if (!accepts_work()) {
        return k_invalid_job_id;
    }

    const JobId job_id = m_next_job_id++;
    m_queue.push_back(PendingJob{job_id, std::move(body)});
    start_next();
    return job_id;
}

void PresetUpdaterInteractor::start_next()
{
    if (!accepts_work() || m_running.has_value() || m_queue.empty()) {
        return;
    }

    Platform::JobManager::JobManager* jobs = job_manager_or_null();
    if (jobs == nullptr) {
        std::deque<PendingJob> abandoned;
        abandoned.swap(m_queue);
        for (const PendingJob& job : abandoned) {
            dispatch_error(
                job.id,
                "PresetUpdater cannot run: no job manager is installed.",
                PresetUpdaterReason::Internal
            );
            dispatch_job_finished(job.id, JobState::Failed);
        }
        return;
    }

    PendingJob job = std::move(m_queue.front());
    m_queue.pop_front();
    m_running = job.id;

    jobs->create_job(job_manager_name(job.id), job.body, job.id)
        .on_result([this, id = job.id](JobState state) { finish_running(id, state); })
        .on_exception(
            [this, id = job.id](const std::exception_ptr& exception)
            {
                const std::string message = exception_message(exception);
                SPDLOG_ERROR("{}", message);
                dispatch_error(id, message, PresetUpdaterReason::Internal);
                finish_running(id, JobState::Failed);
            }
        )
        .start();
}

void PresetUpdaterInteractor::finish_running(JobId job_id, JobState state)
{
    if (m_running.has_value() && *m_running == job_id) {
        m_running.reset();
    }
    dispatch_job_finished(job_id, state);
    start_next();
}

bool PresetUpdaterInteractor::request_running_stop()
{
    if (!m_running.has_value()) {
        return false;
    }
    Platform::JobManager::JobManager* jobs = job_manager_or_null();
    if (jobs == nullptr) {
        return false;
    }
    return jobs->request_job_stop(job_manager_name(*m_running));
}

bool PresetUpdaterInteractor::cancel_job(JobId job_id)
{
    if (job_id == k_invalid_job_id) {
        return false;
    }
    if (m_running.has_value() && *m_running == job_id) {
        request_running_stop();
        return true;
    }
    const auto it = std::find_if(m_queue.begin(), m_queue.end(), [job_id](const PendingJob& job) {
        return job.id == job_id;
    });
    if (it == m_queue.end()) {
        return false;
    }
    m_queue.erase(it);
    dispatch_job_finished(job_id, JobState::Canceled);
    return true;
}

void PresetUpdaterInteractor::cancel_all_jobs()
{
    std::deque<PendingJob> dropped;
    dropped.swap(m_queue);
    request_running_stop();
    for (const PendingJob& job : dropped) {
        dispatch_job_finished(job.id, JobState::Canceled);
    }
}

void PresetUpdaterInteractor::on_notification_cancel()
{
    cancel_all_jobs();
}

size_t PresetUpdaterInteractor::pending_job_count() const
{
    return m_queue.size() + (m_running.has_value() ? 1u : 0u);
}

bool PresetUpdaterInteractor::is_job_pending(JobId job_id) const
{
    if (m_running.has_value() && *m_running == job_id) {
        return true;
    }
    return std::any_of(m_queue.begin(), m_queue.end(), [job_id](const PendingJob& job) {
        return job.id == job_id;
    });
}

std::function<void(const std::string&, int, unsigned)>
PresetUpdaterInteractor::make_status_callback(
    JobId job_id, Platform::JobManager::ProgressTracker progress_tracker, VerboseStyle verbose
)
{
    return [this, job_id, progress_tracker, verbose](
               const std::string& target, int attempt, unsigned delay
           ) mutable
    {
        progress_tracker.set_progress_detail(Domain::ProgressDetail{attempt, target});
        dispatch_status(job_id, target, attempt, delay, verbose);
    };
}

JobId PresetUpdaterInteractor::check_forced_reconfigurations()
{
    //Does not care about App config value - allways runs
    // Forced reconfigurations must be checked on startup

    return enqueue(
        [this](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(job_id, progress_tracker, VerboseStyle::NoProgress),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            std::set<std::string> known_repo_ids;
            for (const SharedPresetUpdaterRepositoryInfo& info :
                 repo_database.get_all_repositories()) {
                known_repo_ids.insert(info.descriptor.id);
            }
            PresetUpdaterReconfigurationList reconfigurations =
                PresetUpdater::check_forced_reconfigurations(known_repo_ids, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            if (stop_token.stop_requested()) {
                return JobState::Canceled;
            }

            dispatch_forced_reconfigurations_list(
                job_id, reconfigurations, process_status.warnings()
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::build_update_sync_and_reconfiguration_check(
    bool online_allowed,
    VerboseStyle verbose,
    bool ignore_hash /*= false*/,
    SourceListSync source_list /*= SourceListSync::Fetch*/,
    const std::set<std::string>& required_repo_ids /*= {}*/
)
{
    return enqueue(
        [this, online_allowed, verbose, ignore_hash, source_list, required_repo_ids](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(job_id, progress_tracker, verbose),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES,
                ignore_hash
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);
            PresetUpdaterRepositorySync archive_sync;

            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            if (source_list == SourceListSync::Fetch && online_allowed) {
                repo_database.sync(&process_status);
                if (process_status.has_error()) {
                    dispatch_error(
                        job_id, process_status.get_error(), process_status.get_error_reason()
                    );
                    return JobState::Failed;
                }
                if (stop_token.stop_requested()) {
                    return JobState::Canceled;
                }
            }

            const SharedRepositoryVector repos =
                repo_database.get_selected_and_required_repositories(required_repo_ids);
            archive_sync.sync(repos, online_allowed, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            if (stop_token.stop_requested()) {
                return JobState::Canceled;
            }

            PresetUpdaterReconfigurationList reconfigurations =
                PresetUpdater::check_reconfigurations(repos, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            if (stop_token.stop_requested()) {
                return JobState::Canceled;
            }

            dispatch_reconfigurations_list(
                job_id, reconfigurations, process_status.warnings(), verbose
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::perform_reconfigurations(
    const PresetUpdaterReconfigurationList& reconfigurations,
    ReconfigurationType types_to_perform /*= ReconfigurationType::All*/
)
{
    // Does not care about App config value - allways runs
    // Possible forced reconfigurations must be able to be performed

    return enqueue(
        [this, reconfigurations, types_to_perform](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(
                    job_id, progress_tracker, VerboseStyle::ProgressNotification
                ),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );

            PresetUpdater::perform_reconfigurations(
                reconfigurations, types_to_perform, &process_status
            );
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            dispatch_reconfigurations_performed(job_id, process_status.warnings());
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::apply_repository_selection(
    const SharedPresetUpdaterRepositoryInfoVector& repos
)
{
    return enqueue(
        [this, repos](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(
                    job_id, progress_tracker, VerboseStyle::ProgressNotification
                ),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);

            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            repo_database.apply_selection(repos, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            dispatch_repository_selection_performed(
                job_id, repo_database.get_all_repositories(), process_status.warnings()
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::add_local_repository(const boost::filesystem::path& zip_path)
{
    return enqueue(
        [this, zip_path](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(
                    job_id, progress_tracker, VerboseStyle::ProgressNotification
                ),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);

            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            repo_database.add_local_repository(zip_path, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            dispatch_repository_info_vector(
                job_id, repo_database.get_all_repositories(), process_status.warnings()
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::remove_local_repository(const std::string& uuid)
{
    return enqueue(
        [this, uuid](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(
                    job_id, progress_tracker, VerboseStyle::ProgressNotification
                ),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);

            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            repo_database.remove_local_repository(uuid, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            dispatch_repository_info_vector(
                job_id, repo_database.get_all_repositories(), process_status.warnings()
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::list_repositories(bool online_allowed, bool force_sync /*= true*/)
{
    return enqueue(
        [this, force_sync = force_sync && online_allowed](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(
                    job_id, progress_tracker, VerboseStyle::ProgressNotification
                ),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdaterRepositoryDatabase repo_database(&process_status);

            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            if (force_sync) {
                repo_database.sync(&process_status);
                if (process_status.has_error()) {
                    dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                    return JobState::Failed;
                }
            }

            dispatch_repository_info_vector(
                job_id, repo_database.get_all_repositories(), process_status.warnings()
            );
            return JobState::Succeeded;
        }
    );
}

JobId PresetUpdaterInteractor::cleanup_update_sync()
{
    return enqueue(
        [this](
            JThread::StopToken stop_token,
            Platform::JobManager::ProgressTracker progress_tracker,
            JobId job_id
        ) {
            PresetUpdaterProcessStatus process_status(
                stop_token,
                make_status_callback(job_id, progress_tracker, VerboseStyle::NoProgress),
                PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES
            );
            PresetUpdater::cleanup_update_sync(&process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            PresetUpdaterRepositoryDatabase repo_database(&process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }

            const SharedRepositoryVector& repos = repo_database.get_selected_repositories();
            PresetUpdaterReconfigurationList reconfigurations =
                PresetUpdater::check_reconfigurations(repos, &process_status);
            if (process_status.has_error()) {
                dispatch_error(job_id, process_status.get_error(), process_status.get_error_reason());
                return JobState::Failed;
            }
            if (stop_token.stop_requested()) {
                return JobState::Canceled;
            }
            dispatch_reconfigurations_list(
                job_id, reconfigurations, process_status.warnings(), VerboseStyle::NoProgress
            );
            return JobState::Succeeded;
        }
    );
}

void PresetUpdaterInteractor::dispatch_error(
    JobId job_id, const std::string& body, PresetUpdaterReason reason
)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, body, reason]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, body, reason](auto* listener) {
            listener->on_preset_updater_error(job_id, body, reason);
        });
    });
}

void PresetUpdaterInteractor::dispatch_status(JobId job_id, const std::string& target, int attempt, unsigned delay, VerboseStyle verbose)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, target, attempt, delay, verbose]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, target, attempt, delay, verbose](auto* listener) {
            listener->on_preset_updater_status(job_id, target, attempt, delay, verbose);
        });
    });
}

void PresetUpdaterInteractor::dispatch_forced_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, reconfigurations, warnings]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, reconfigurations, warnings](auto* listener) {
            listener->on_preset_updater_forced_reconfigurations_list(job_id, reconfigurations, warnings);
        });
    });
}

void PresetUpdaterInteractor::dispatch_reconfigurations_list(
    JobId job_id,
    const PresetUpdaterReconfigurationList& reconfigurations,
    const std::vector<PresetUpdaterWarning>& warnings,
    VerboseStyle verbose
)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, reconfigurations, warnings, verbose]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, reconfigurations, warnings, verbose](auto* listener) {
            listener->on_preset_updater_reconfigurations_list(job_id, reconfigurations, warnings, verbose);
        });
    });
}

void PresetUpdaterInteractor::dispatch_reconfigurations_performed(JobId job_id, const std::vector<PresetUpdaterWarning>& warnings)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, warnings]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, warnings](auto* listener) {
            listener->on_preset_updater_reconfigurations_performed(job_id, warnings);
        });
    });
}

void PresetUpdaterInteractor::dispatch_repository_info_vector(
    JobId job_id,
    const SharedPresetUpdaterRepositoryInfoVector& repos,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, repos, warnings]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, repos, warnings](auto* listener) {
            listener->on_preset_updater_repository_info_vector(job_id, repos, warnings);
        });
    });
}

void PresetUpdaterInteractor::dispatch_repository_selection_performed(
    JobId job_id,
    const SharedPresetUpdaterRepositoryInfoVector& repos,
    const std::vector<PresetUpdaterWarning>& warnings
)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, repos, warnings]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, repos, warnings](auto* listener) {
            listener->on_preset_updater_repository_selection_performed(job_id, repos, warnings);
        });
    });
}

void PresetUpdaterInteractor::dispatch_job_finished(JobId job_id, JobState state)
{
    m_dispatcher.dispatch_on_main_thread([this, job_id, state]() {
        this->invoke_listeners<IPresetUpdaterResultListener>([job_id, state](auto* listener) {
            listener->on_preset_updater_job_finished(job_id, state);
        });
    });
}

} // namespace Slic3r::Biz::PresetUpdater
