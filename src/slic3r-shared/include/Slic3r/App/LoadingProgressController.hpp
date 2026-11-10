#pragma once

#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <set>
#include <string>

namespace Slic3r::Biz::Platform::JobManager {
class JobManager;
} // namespace Slic3r::Biz::Platform::JobManager

namespace Slic3r::App {

class Navigator;

struct ILoadingProgressChangedListener
{
    virtual void on_loading_progress_changed(const std::string& label, int percent) = 0;
    virtual ~ILoadingProgressChangedListener() = default;
};

/**
 * @brief Tracks file loading jobs (see Biz::FileOpen::FILE_LOAD_JOB_PREFIX) and shows their
 * progress in ModalDialog::LoadingProgress through the Navigator.
 *
 * The dialog opens when loading runs longer than a short delay, so quick loads don't flash it,
 * and closes shortly after the last loading job ends, so it stays open between jobs which follow
 * each other. It doesn't replace another modal dialog, it opens once that one is closed.
 *
 * More jobs may run at once, the dialog shows the progress of one of them.
 */
class LoadingProgressController :
    public WithListeners<ILoadingProgressChangedListener>,
    public Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
public:
    LoadingProgressController(
        Navigator& navigator,
        Biz::Platform::JobManager::JobManager& job_manager,
        Biz::Platform::TimerQueue& timer_queue
    );

    /**
     * @brief Stops listening to the job manager, to be called before the platform services close.
     */
    void shutdown();

    void on_job_manager_status_changed(
        const Biz::Platform::JobManager::JobManagerStatus& status
    ) override;

    /**
     * @brief Asks all loading jobs to stop, also the ones which start until the dialog closes.
     * The dialog stays open until the jobs end.
     */
    void cancel();

    const std::string& label() const;
    /// 0..100
    int percent() const;

private:
    void request_stop_of_jobs();
    void start_open_timer();
    void start_close_timer();
    /**
     * @brief Closes the dialog and forgets the finished loading.
     */
    void finish();
    void update_progress(const Biz::Platform::JobManager::Progress& progress);
    void open_dialog_if_possible();
    void cancel_timer(Biz::Platform::TimerQueue::TimerID& timer_id);
    void notify_progress_changed();

private:
    Navigator& m_navigator;
    Biz::Platform::JobManager::JobManager& m_job_manager;
    Biz::Platform::TimerQueue& m_timer_queue;
    bool m_shut_down{false};

    std::set<std::string> m_job_names;
    /// Job whose progress is shown, empty when the loading finished.
    std::string m_shown_job;
    Biz::Platform::TimerQueue::TimerID m_open_timer_id;
    Biz::Platform::TimerQueue::TimerID m_close_timer_id;
    bool m_cancelling{false};

    std::string m_label;
    int m_percent{0};
};

} // namespace Slic3r::App
