#pragma once

#include <map>
#include "Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp"

namespace Slic3r::Biz::Platform::JobManager {

using JobManagerStatus = std::map<std::string, Progress>;

struct IJobManagerStatusChangedListener
{
    virtual void on_job_manager_status_changed(const JobManagerStatus&) = 0;
    virtual ~IJobManagerStatusChangedListener()                         = default;
};

} // namespace Slic3r::Biz::Platform::JobManager
