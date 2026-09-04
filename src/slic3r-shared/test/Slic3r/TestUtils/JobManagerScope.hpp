#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Tests {

class JobManagerScope
{
public:
    explicit JobManagerScope(Slic3r::Biz::Platform::IMainThreadDispatcher& dispatcher)
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_job_manager(
            std::make_unique<Slic3r::Biz::Platform::JobManager::JobManager>(dispatcher)
        );
    }

    ~JobManagerScope()
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_job_manager(nullptr);
    }

    JobManagerScope(const JobManagerScope&)            = delete;
    JobManagerScope& operator=(const JobManagerScope&) = delete;
};

} // namespace Tests
