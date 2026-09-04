#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/JobStatus.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Log.hpp" // IWYU pragma: keep

namespace Slic3r::Biz::Platform::JobManager {

struct Progress
{
    Domain::JobStatus status;
    Domain::ProgressDetail progress_detail;
    std::optional<Domain::Percentage> percent;
    Domain::SelectionId project_id{Domain::INVALID_ID};
};

class ProgressTracker
{
public:
    ProgressTracker(IMainThreadDispatcher& dispatcher, std::function<void(Progress)> on_change);

    void set_status(const Domain::JobStatus status);
    void set_status_unsafe(const Domain::JobStatus status);
    void set_project_id(const Domain::SelectionId project_id);
    void set(Domain::Percentage percentage);
    const Progress& get_progress() const;
    void set_progress_detail(Domain::ProgressDetail progress_detail);

private:
    // All these data must only be accessed from the main thread!
    std::shared_ptr<Progress> m_progress{std::make_shared<Progress>()};
    std::reference_wrapper<IMainThreadDispatcher> m_dispatcher;
    std::function<void(Progress)> m_on_change;
};
} // namespace Slic3r::Biz::Platform::JobManager
