#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterJob.hpp"
#include "Slic3r/Biz/PresetUpdater/PresetUpdaterProgress.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz::PresetUpdater {
class PresetUpdaterInteractor;
} // namespace Slic3r::Biz::PresetUpdater

namespace Slic3r::App {

class PresetUpdaterStatusNotification
{
public:
    enum class Activity
    {
        ListingSources,
        ApplyingSelection,
        AddingSource,
        RemovingSource,
        Checking,
        Installing,
        RemovingVendor
    };

    explicit PresetUpdaterStatusNotification(
        Biz::PresetUpdater::PresetUpdaterInteractor& preset_updater_interactor
    );

    /// Registers a job as work the user asked for and refreshes the notification.
    void begin_activity(Biz::PresetUpdater::JobId job_id, Activity activity);

    /// Retires a job. The notification closes once nothing the user asked for is left.
    void end_activity(Biz::PresetUpdater::JobId job_id);

    bool tracks(Biz::PresetUpdater::JobId job_id) const;

    /// Names what the running job is working on. Ignored for an untracked job.
    void report_progress(
        Biz::PresetUpdater::JobId job_id, const Biz::PresetUpdater::PresetUpdaterProgress& progress
    );

    void refresh();

    /// Drops what was collected. For teardown only, so it never touches the notification center.
    void reset();

private:
    Biz::PresetUpdater::PresetUpdaterInteractor& m_preset_updater_interactor;

    /// Insertion ordered, so the front entry is the one the user is waiting on.
    std::vector<std::pair<Biz::PresetUpdater::JobId, Activity>> m_activities;
    Biz::PresetUpdater::PresetUpdaterProgress m_activity_progress;
};

} // namespace Slic3r::App
