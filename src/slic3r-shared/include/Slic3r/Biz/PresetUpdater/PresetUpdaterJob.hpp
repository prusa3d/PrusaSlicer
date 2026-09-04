#pragma once

#include <cstdint>

namespace Slic3r::Biz::PresetUpdater {

using JobId = std::uint64_t;

inline constexpr JobId k_invalid_job_id = 0;

enum class JobState
{
    Succeeded,
    Failed,
    Canceled,
};

} // namespace Slic3r::Biz::PresetUpdater
