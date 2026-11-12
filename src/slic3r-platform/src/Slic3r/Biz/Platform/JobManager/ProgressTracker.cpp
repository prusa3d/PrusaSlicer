#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"

namespace Slic3r::Biz::Platform::JobManager {

ProgressTracker::ProgressTracker(IMainThreadDispatcher& dispatcher, std::function<void(Progress)> on_change) :
    m_dispatcher{dispatcher},
    m_on_change{on_change}
{}

void ProgressTracker::set_status(const Domain::JobStatus status)
{
    if (!m_dispatcher.get().dispatch_on_main_thread(
            [on_change = m_on_change, progress = m_progress, status]()
            {
                progress->status = status;
                on_change(*progress);
            }
        ))
    {
        SPDLOG_WARN("status not emitted");
    }
}

void ProgressTracker::set_status_unsafe(const Domain::JobStatus status)
{
    m_progress->status = status;
    m_on_change(*m_progress);
}

void ProgressTracker::set_project_id(const Domain::SelectionId project_id)
{
    m_progress->project_id = project_id;
}

void ProgressTracker::set(Domain::Percentage percentage)
{
    if (!m_dispatcher.get().dispatch_on_main_thread(
            [on_change = m_on_change, progress = m_progress, percentage]()
            {
                ASSERT(progress->status == Domain::JobStatus::Started);
                ASSERT(Domain::Percentage{0} <= percentage && percentage <= Domain::Percentage{100});
                if (progress->percent) {
                    ASSERT(percentage >= progress->percent);
                }
                progress->percent = percentage;
                on_change(*progress);
            }
        ))
    {
        SPDLOG_WARN("progress not emitted");
    }
}

const Progress& ProgressTracker::get_progress() const
{
    return *m_progress;
}

void ProgressTracker::set_progress_detail(Domain::ProgressDetail progress_detail)
{
    if (!m_dispatcher.get().dispatch_on_main_thread(
            [on_change = m_on_change, progress = m_progress, pd = std::move(progress_detail)]() mutable
            {
                progress->progress_detail = std::move(pd);
                on_change(*progress);
            }
        ))
    {
        SPDLOG_WARN("progress detail not emitted");
    }
}

} // namespace Slic3r::Biz::Platform::JobManager
