#pragma once

#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

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
 * The dialog opens when a loading job runs longer than a short delay, so quick loads don't flash
 * it, and closes when the job ends. It doesn't replace another modal dialog, it opens once that
 * one is closed.
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
     * @brief Asks the tracked job to stop, the dialog stays open until the job ends.
     */
    void cancel();

    const std::string& label() const;
    /// 0..100
    int percent() const;

private:
    void start_tracking(const std::string& job_name);
    void stop_tracking();
    void update_progress(const Biz::Platform::JobManager::Progress& progress);
    void open_dialog_if_possible();
    void cancel_open_timer();
    void notify_progress_changed();

private:
    Navigator& m_navigator;
    Biz::Platform::JobManager::JobManager& m_job_manager;
    Biz::Platform::TimerQueue& m_timer_queue;
    bool m_shut_down{false};

    /// Name of the tracked job, empty when no loading job runs.
    std::string m_job_name;
    Biz::Platform::TimerQueue::TimerID m_open_timer_id;
    bool m_cancelling{false};

    std::string m_label;
    int m_percent{0};
};

} // namespace Slic3r::App
