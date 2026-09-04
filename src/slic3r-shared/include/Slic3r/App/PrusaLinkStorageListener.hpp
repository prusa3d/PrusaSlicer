#pragma once

#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App::PrintHost {

class PrusaLinkStorageListener : public Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
public:
    PrusaLinkStorageListener(Biz::ProjectInteractor& project_interactor) :
        m_project_interactor(project_interactor)
    {}

    void on_job_manager_status_changed(
        const Biz::Platform::JobManager::JobManagerStatus& status
    ) override;

private:
    Biz::ProjectInteractor& m_project_interactor;
};
} // namespace Slic3r::App::PrintHost
