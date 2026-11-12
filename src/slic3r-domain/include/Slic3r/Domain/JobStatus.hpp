#pragma once

#include <any>

namespace Slic3r::Domain {

enum class JobStatus
{
    None,
    Started,
    Finished,
    Failed
};

struct ProgressDetail
{
    // Each job should implement own ProgressInfo enum.
    // Here it is represented with int 'info'.
    // See SlicingProgressInfo f.e.
    // ProgressDetail is part of Domain to be seen from Slicing core.
    // It is stored in Biz::Platform::JobManager::Progress.
    // Progress itself is shared from JobManager in
    // JobManagerStatus - a map of string job name and Progress.
    // Thus it should be always clear which enum 'info' refers to.
    int info{0};
    std::any payload;
};

} // namespace Slic3r::Domain
