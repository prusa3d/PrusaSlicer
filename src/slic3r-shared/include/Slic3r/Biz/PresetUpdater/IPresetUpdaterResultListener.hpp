#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterJob.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReconfigurationList.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterRepositoryDescriptor.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterWarning.hpp"
#include <vector>
namespace Slic3r::Biz::PresetUpdater {

class IPresetUpdaterResultListener
{
public:
    virtual ~IPresetUpdaterResultListener() = default;

    virtual void on_preset_updater_error(
        JobId job_id, const std::string& body, PresetUpdaterReason reason
    ) = 0;
    virtual void on_preset_updater_forced_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings
    ) = 0;
    virtual void on_preset_updater_reconfigurations_list(
        JobId job_id,
        const PresetUpdaterReconfigurationList& reconfigurations,
        const std::vector<PresetUpdaterWarning>& warnings,
        VerboseStyle verbose
    ) = 0;
    virtual void on_preset_updater_reconfigurations_performed(
        JobId job_id,
        const std::vector<PresetUpdaterWarning>& warnings
    ) {};
    virtual void on_preset_updater_status(
        JobId job_id, const std::string& target, int attempt, unsigned delay, VerboseStyle verbose
    ) = 0;
    virtual void on_preset_updater_repository_info_vector(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    ) {};

    virtual void on_preset_updater_repository_selection_performed(
        JobId job_id,
        const SharedPresetUpdaterRepositoryInfoVector& descriptor,
        const std::vector<PresetUpdaterWarning>& warnings
    ) {};

    virtual void on_preset_updater_job_finished(
        JobId job_id, JobState state
    ) {};
};
} // namespace Slic3r::Biz::PresetUpdater
