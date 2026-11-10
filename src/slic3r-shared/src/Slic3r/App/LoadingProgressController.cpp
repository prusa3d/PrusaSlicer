#include "Slic3r/App/LoadingProgressController.hpp"

#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Biz/FileOpen/FileLoadProgress.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"

#include <fmt/format.h>

#include <any>
#include <chrono>

namespace Slic3r::App {

using Biz::FileOpen::FileLoadStage;
using Biz::Platform::JobManager::IJobManagerStatusChangedListener;
using Biz::Platform::JobManager::JobManagerStatus;
using Biz::Platform::JobManager::Progress;

namespace {

/// Delay before the dialog opens, and the interval of the next attempts while another modal
/// dialog is open.
constexpr std::chrono::milliseconds OPEN_DELAY{400};
/// Delay before the dialog closes, a job which follows the finished one starts meanwhile.
constexpr std::chrono::milliseconds CLOSE_DELAY{100};

std::string progress_label(const Domain::ProgressDetail& detail)
{
    const auto* payload{std::any_cast<Biz::FileOpen::FileLoadProgressPayload>(&detail.payload)};
    if (payload == nullptr) {
        // TRN Loading progress dialog. Shown before the first file is processed.
        return fmt::format("{}...", Biz::_u8L("Loading"));
    }

    switch (static_cast<FileLoadStage>(detail.info)) {
    case FileLoadStage::Reading:
        if (payload->file_count > 1) {
            return fmt::format(
                // TRN Loading progress dialog. {0} is a file name, {1} is its order number,
                // {2} is the count of the loaded files.
                fmt::runtime(Biz::_u8L("Reading {0} ({1} of {2})")),
                payload->file_name,
                payload->file_index + 1,
                payload->file_count
            );
        }
        // TRN Loading progress dialog. {} is a file name.
        return fmt::format(fmt::runtime(Biz::_u8L("Reading {}")), payload->file_name);
    case FileLoadStage::Converting:
        // TRN Loading progress dialog. {} is a file name.
        return fmt::format(fmt::runtime(Biz::_u8L("Processing {}")), payload->file_name);
    case FileLoadStage::AddingToScene:
        if (payload->file_name.empty()) {
            // TRN Loading progress dialog. Shown when more files are added at once.
            return Biz::_u8L("Adding the files to the scene");
        }
        // TRN Loading progress dialog. {} is a file name.
        return fmt::format(fmt::runtime(Biz::_u8L("Adding {} to the scene")), payload->file_name);
    }
    return {};
}

} // namespace

LoadingProgressController::LoadingProgressController(
    Navigator& navigator,
    Biz::Platform::JobManager::JobManager& job_manager,
    Biz::Platform::TimerQueue& timer_queue
) :
    m_navigator(navigator),
    m_job_manager(job_manager),
    m_timer_queue(timer_queue)
{
    m_job_manager.add_listener<IJobManagerStatusChangedListener>(this);
}

void LoadingProgressController::shutdown()
{
    if (m_shut_down) {
        return;
    }
    m_shut_down = true;
    m_job_names.clear();
    m_shown_job.clear();
    cancel_timer(m_open_timer_id);
    cancel_timer(m_close_timer_id);
    m_job_manager.remove_listener<IJobManagerStatusChangedListener>(this);
}

void LoadingProgressController::on_job_manager_status_changed(const JobManagerStatus& status)
{
    m_job_names.clear();
    for (const auto& [job_name, progress] : status) {
        if (job_name.starts_with(Biz::FileOpen::FILE_LOAD_JOB_PREFIX)
            && progress.status == Domain::JobStatus::Started)
        {
            m_job_names.insert(job_name);
        }
    }

    if (m_job_names.empty()) {
        if (!m_shown_job.empty()) {
            start_close_timer();
        }
        return;
    }

    cancel_timer(m_close_timer_id);
    if (m_cancelling) {
        request_stop_of_jobs();
    }
    if (!m_job_names.contains(m_shown_job)) {
        m_shown_job = *m_job_names.begin();
    }
    start_open_timer();
    update_progress(status.at(m_shown_job));
}

void LoadingProgressController::cancel()
{
    if (m_job_names.empty()) {
        return;
    }
    request_stop_of_jobs();
    m_cancelling = true;
    // TRN Loading progress dialog. Shown after the user cancelled the loading.
    m_label = fmt::format("{}...", Biz::_u8L("Cancelling"));
    notify_progress_changed();
}

const std::string& LoadingProgressController::label() const
{
    return m_label;
}

int LoadingProgressController::percent() const
{
    return m_percent;
}

void LoadingProgressController::request_stop_of_jobs()
{
    for (const std::string& job_name : m_job_names) {
        m_job_manager.request_job_stop(job_name);
    }
}

void LoadingProgressController::start_open_timer()
{
    if (m_timer_queue.is_timer_running(m_open_timer_id)) {
        return;
    }
    // Repeats while the jobs run: the dialog opens once no other modal dialog is open, and
    // opens again when another modal dialog replaced it meanwhile.
    m_open_timer_id = m_timer_queue.set_timer(
        OPEN_DELAY,
        [this]() { open_dialog_if_possible(); },
        true
    );
}

void LoadingProgressController::start_close_timer()
{
    if (m_timer_queue.is_timer_running(m_close_timer_id)) {
        return;
    }
    m_close_timer_id = m_timer_queue.set_timer(CLOSE_DELAY, [this]() { finish(); });
}

void LoadingProgressController::finish()
{
    // The callback may already be dispatched when a next job starts and the timer is cancelled.
    if (!m_job_names.empty()) {
        return;
    }
    m_shown_job.clear();
    m_cancelling = false;
    cancel_timer(m_open_timer_id);
    cancel_timer(m_close_timer_id);
    if (m_navigator.current_modal_dialog() == ModalDialog::LoadingProgress) {
        m_navigator.set_modal_dialog(ModalDialog::None);
    }
}

void LoadingProgressController::update_progress(const Progress& progress)
{
    if (m_cancelling) {
        return;
    }
    m_label   = progress_label(progress.progress_detail);
    m_percent = progress.percent ? static_cast<int>(progress.percent->value * 100.) : 0;
    notify_progress_changed();
}

void LoadingProgressController::open_dialog_if_possible()
{
    // The callback may already be dispatched when the jobs end and the timer is cancelled.
    if (m_job_names.empty()) {
        return;
    }
    if (m_navigator.current_modal_dialog() == ModalDialog::None) {
        m_navigator.set_modal_dialog(ModalDialog::LoadingProgress);
    }
}

void LoadingProgressController::cancel_timer(Biz::Platform::TimerQueue::TimerID& timer_id)
{
    if (m_timer_queue.is_timer_running(timer_id)) {
        m_timer_queue.cancel_timer(timer_id);
    }
    timer_id = {};
}

void LoadingProgressController::notify_progress_changed()
{
    invoke_listeners<ILoadingProgressChangedListener>(
        [this](ILoadingProgressChangedListener* listener)
        { listener->on_loading_progress_changed(m_label, m_percent); }
    );
}

} // namespace Slic3r::App
