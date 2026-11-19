#include "Slic3r/App/LoadingProgressDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ProgressBar.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/FileOpen/FileLoadJob.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

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

LoadingProgressDialog::LoadingProgressDialog(
    Navigator& navigator,
    Biz::Platform::JobManager::JobManager& job_manager,
    Biz::Platform::TimerQueue& timer_queue
) :
    // TRN Loading progress dialog title.
    Dialog({Biz::_u8L("Loading")}, "LoadingProgressDialog"),
    m_navigator(navigator),
    m_job_manager(job_manager),
    m_timer_queue(timer_queue)
{
    using namespace Yoga;

    set_closable(false);

    content_item()->set_modal(true);
    content_item()->set_width(400_fpx);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(10_fpx);

    m_label = content()->emplace_back<Text>(std::string{});
    m_label->set_wrap_mode(Text::WrapMode::Wrap);

    m_progress_bar = content()->emplace_back<ProgressBar>();
    m_progress_bar->set_height(20_fpx);

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_justify_content(YGJustifyFlexEnd);

    LayoutButton* cancel_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->set_content_padding(Paddings(15_fpx, 5_fpx));
    cancel_button->callbacks().action = [this]() { cancel(); };

    m_job_manager.add_listener<IJobManagerStatusChangedListener>(this);
}

LoadingProgressDialog::~LoadingProgressDialog()
{
    shutdown();
}

void LoadingProgressDialog::shutdown()
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

void LoadingProgressDialog::on_job_manager_status_changed(const JobManagerStatus& status)
{
    const std::set<std::string> previous_job_names{std::move(m_job_names)};
    m_job_names.clear();
    for (const auto& [job_name, progress] : status) {
        if (job_name.starts_with(Biz::FileOpen::FILE_LOAD_JOB_PREFIX)
            && progress.status == Domain::JobStatus::Started)
        {
            m_job_names.insert(job_name);
            // The already running jobs were asked to stop before.
            if (m_cancelling && !previous_job_names.contains(job_name)) {
                m_job_manager.request_job_stop(job_name);
            }
        }
    }

    if (m_job_names.empty()) {
        if (!m_shown_job.empty()) {
            start_close_timer();
        }
        return;
    }

    cancel_timer(m_close_timer_id);
    m_navigator.ensure_module_type(Render::ModuleType::Plater);
    if (!m_job_names.contains(m_shown_job)) {
        m_shown_job = *m_job_names.begin();
    }
    start_open_timer();
    update_progress(status.at(m_shown_job));
}

const std::string& LoadingProgressDialog::label() const
{
    return m_label->text();
}

int LoadingProgressDialog::percent() const
{
    return static_cast<int>(m_progress_bar->progress());
}

void LoadingProgressDialog::cancel()
{
    if (m_job_names.empty()) {
        return;
    }
    request_stop_of_jobs();
    m_cancelling = true;
    // TRN Loading progress dialog. Shown after the user cancelled the loading.
    show_progress(fmt::format("{}...", Biz::_u8L("Cancelling")), percent());
}

void LoadingProgressDialog::request_stop_of_jobs()
{
    for (const std::string& job_name : m_job_names) {
        m_job_manager.request_job_stop(job_name);
    }
}

void LoadingProgressDialog::start_open_timer()
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

void LoadingProgressDialog::start_close_timer()
{
    if (m_timer_queue.is_timer_running(m_close_timer_id)) {
        return;
    }
    m_close_timer_id = m_timer_queue.set_timer(CLOSE_DELAY, [this]() { finish(); });
}

void LoadingProgressDialog::finish()
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

void LoadingProgressDialog::update_progress(const Progress& progress)
{
    if (m_cancelling) {
        return;
    }
    show_progress(
        progress_label(progress.progress_detail),
        progress.percent ? static_cast<int>(progress.percent->value * 100.) : 0
    );
}

void LoadingProgressDialog::show_progress(const std::string& label, int percent)
{
    m_label->set_text(label);
    m_progress_bar->set_progress(percent);
    if (opened()) {
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
}

void LoadingProgressDialog::open_dialog_if_possible()
{
    // The callback may already be dispatched when the jobs end and the timer is cancelled.
    if (m_job_names.empty()) {
        return;
    }
    // The selected project, and so the module type, may change during the loading.
    m_navigator.ensure_module_type(Render::ModuleType::Plater);
    if (m_navigator.current_modal_dialog() == ModalDialog::None) {
        m_navigator.set_modal_dialog(ModalDialog::LoadingProgress);
    }
}

void LoadingProgressDialog::cancel_timer(Biz::Platform::TimerQueue::TimerID& timer_id)
{
    if (m_timer_queue.is_timer_running(timer_id)) {
        m_timer_queue.cancel_timer(timer_id);
    }
    timer_id = {};
}

} // namespace Slic3r::App
