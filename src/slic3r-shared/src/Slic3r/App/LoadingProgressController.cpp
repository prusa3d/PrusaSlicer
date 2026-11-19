#include "Slic3r/App/LoadingProgressController.hpp"

#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Biz/FileOpen/FileLoadProgress.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"

#include <fmt/format.h>

#include <algorithm>
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
    m_job_name.clear();
    cancel_open_timer();
    m_job_manager.remove_listener<IJobManagerStatusChangedListener>(this);
}

void LoadingProgressController::on_job_manager_status_changed(const JobManagerStatus& status)
{
    const auto it{std::ranges::find_if(
        status,
        [](const auto& job)
        {
            return job.first.starts_with(Biz::FileOpen::FILE_LOAD_JOB_PREFIX)
                && job.second.status == Domain::JobStatus::Started;
        }
    )};

    if (it == status.end()) {
        stop_tracking();
        return;
    }

    const auto& [job_name, progress] = *it;
    if (job_name != m_job_name) {
        start_tracking(job_name);
    }
    update_progress(progress);
}

void LoadingProgressController::cancel()
{
    if (m_job_name.empty()) {
        return;
    }
    m_job_manager.request_job_stop(m_job_name);
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

void LoadingProgressController::start_tracking(const std::string& job_name)
{
    // The dialog stays as it is when a next job follows, e.g. when projects load one by one.
    m_job_name   = job_name;
    m_cancelling = false;
    if (m_timer_queue.is_timer_running(m_open_timer_id)) {
        return;
    }

    // Repeats while the job runs: the dialog opens once no other modal dialog is open, and
    // opens again when another modal dialog replaced it meanwhile.
    m_open_timer_id = m_timer_queue.set_timer(
        OPEN_DELAY,
        [this]() { open_dialog_if_possible(); },
        true
    );
}

void LoadingProgressController::stop_tracking()
{
    if (m_job_name.empty()) {
        return;
    }

    m_job_name.clear();
    m_cancelling = false;
    cancel_open_timer();
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
    // The callback may already be dispatched when the job ends and the timer is cancelled.
    if (m_job_name.empty()) {
        return;
    }
    if (m_navigator.current_modal_dialog() == ModalDialog::None) {
        m_navigator.set_modal_dialog(ModalDialog::LoadingProgress);
    }
}

void LoadingProgressController::cancel_open_timer()
{
    if (m_timer_queue.is_timer_running(m_open_timer_id)) {
        m_timer_queue.cancel_timer(m_open_timer_id);
    }
}

void LoadingProgressController::notify_progress_changed()
{
    invoke_listeners<ILoadingProgressChangedListener>(
        [this](ILoadingProgressChangedListener* listener)
        { listener->on_loading_progress_changed(m_label, m_percent); }
    );
}

} // namespace Slic3r::App
